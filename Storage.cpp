#include "Storage.h"
#include "Config.h"
#include "Display.h"

SdFatSoftSpi<SOFT_MISO_PIN, SOFT_MOSI_PIN, SOFT_SCK_PIN> SD;

#include <string.h>

/**
 * @brief      Determines if file is a bitmap
 *
 * @param      file Reference to the file
 *
 * @return     True on success, False otherwise
 */
static bool _isBitmap(File &file)
{
    file.seek(0);
    return file.read() == 'B' && file.read() == 'M';
}

Storage::Storage()
{
    Serial.println("Initializing Storage...");

    // Set default values for private members
    if (!SD.begin(SD_CHIP_SELECT_PIN))
    {
        Serial.println("Failed to initialize SD card");
    }

    this->bitmap.data = new uint8_t [1]; // Avoid freeing empty arrays

    // Compress all bitmaps found
    Display::instance().clear();
    Display::instance().renderText("Device is starting...", SCREEN_X / 2, 200);
    Display::instance().renderText("Do NOT turn off the power now!", SCREEN_X / 2, 220);
    delay(1000);

    // Loop over all bitmaps found in the root
    File root = SD.open("/");
    File entry = root.openNextFile();
    while (entry)
    {
        if (_isBitmap(entry))
        {
            char filename[20];
            entry.getName(filename, 20);
            // getBitmap automatically compresses if it is already not done
            getBitmap(filename, 0, 0);
        }

        // Open next file
        entry = root.openNextFile();
    }
    entry.close();
    root.close();

    // Create the monochrome color for Display if it doesn't exist
    uint16_t mono_color = 0xF800; // Red by default
    char filename[20] = "monocolor";
    if (!fileOpenToRead(filename))
    {
        fileOpenToWrite(filename);
        fileWriteData((uint8_t *)&mono_color, 2);
        fileClose();
        // Serial.println("Wrote mono_color 0xF800 to file 'monocolor'");
    }

    Serial.println("...Storage initialized");
}

/**
 * @brief      Reads DIB type
 *
 * @param      file Reference to the file
 *
 * @return     Size of DIB in bytes
 */
static uint16_t _readDIB(File &file)
{
    // Print DIB size for debugging
    file.seek(0x0E);
    uint16_t size_DIB =
        ((uint16_t)file.read() << 0) +
        ((uint16_t)file.read() << 8);
    return size_DIB;
}

/**
 * @brief      Reads offset where bitmap data begins
 *
 * @param      file Reference to the file
 *
 * @return     Offset value
 */
static uint32_t _readOffset(File &file)
{
    // Read offset where the data begins
    file.seek(0x0A);
    uint32_t offset =
        ((uint32_t)file.read()) +
        ((uint32_t)file.read() << 8) +
        ((uint32_t)file.read() << 16) +
        ((uint32_t)file.read() << 24);
    return offset;
}

/**
 * @brief      Reads bitmap width if known bitmap type
 *
 * @param      file Reference to the file
 *
 * @return     Width of bitmap
 */
static int32_t _readWidth(File &file)
{
    if (_readDIB(file) == 40)
    {
        file.seek(0x12);
        return
            ((int32_t)file.read()) +
            ((int32_t)file.read() << 8) +
            ((int32_t)file.read() << 16) +
            ((int32_t)file.read() << 24);
    }
    else
    {
        return -1;
    }
}

/**
 * @brief      Reads bitmap height if known bitmap type
 *
 * @param      file Reference to the file
 *
 * @return     Height of bitmap
 */
static int32_t _readHeight(File &file)
{
    if (_readDIB(file) == 40)
    {
        file.seek(0x16);
        int32_t height =
            ((int32_t)file.read()) +
            ((int32_t)file.read() << 8) +
            ((int32_t)file.read() << 16) +
            ((int32_t)file.read() << 24);

        // Height might be negative
        // See BITMAPV4HEADER references or similar
        // Some weird interaction also with abs() and int32_t
        // Produced totally erroneous results
        if (height < 0)
            height = -height;
        return height;
    }
    else
    {
        return -1;
    }
}

/**
 * @brief      Read how many bits are used for each pixel
 *
 * @param      file Reference to the file
 *
 * @return     Amount of bits used per pixel
 */
static uint8_t _readBitsPerPixel(File &file)
{
    // Read how many bits are there per pixel in the file
    if (_readDIB(file) == 40)
    {
        file.seek(0x1C);
        return file.read();
    }
}

/**
 * @brief      Read what compression method the bitmap uses
 *
 * @param      file Reference to the file
 *
 * @return     Compression method type
 */
static uint8_t _readCompressionMethod(File &file)
{
    if (_readDIB(file) == 40)
    {
        file.seek(0x1E);
        return file.read();
    }
}

/**
 * @brief      Converts RGB888 data into RGB565 format
 *
 * @param      red      Red byte of data to convert from
 * @param      green    Green byte of data to convert from
 * @param      blue     Blue byte of data to convert from
 */
static uint16_t _RGB888ToRGB565(uint8_t red, uint8_t green, uint8_t blue)
{
    // Drop least significant bits
    return ((red & 0xF8) << 8) | ((green & 0xFC) << 3) | (blue & 0xF8) >> 3;
}

/**
 * @brief      Writes a compressed block of data into a specified file
 *
 * @param      filename  The filename to write into
 * @param      row       The row to write into the data
 * @param      start     The start offset of the scanline
 * @param      end       The end offset of the scanline
 */
void Storage::writeCompressedBlock(char filename[], uint16_t row, uint16_t start, uint16_t end)
{
    // Write compressed data into file
    fileOpenToWrite(filename);
    uint8_t data[8] = { (uint8_t)row, (uint8_t)((row & 0xFF00) >> 8),
                        (uint8_t)start, (uint8_t)((start & 0xFF00) >> 8),
                        (uint8_t)end, (uint8_t)((end & 0xFF00) >> 8),
                        0xFF, 0xFF }; // Some padding required to achieve 32-bit writes
    fileWriteData(data, 8);
}

/**
 * @brief      Translate index to pixel memory position
 *
 * @param      x     Index to translate
 *
 * @return     Index in memory
 */
static inline int16_t _translate(uint32_t x)
{
    return ((x / 8) * 2 + 1) * 8 - 1 - x;
}

/**
 * @brief      Get specific bit state from an array of data. Might overflow!
 *
 * @param      data   Data to iterate over
 * @param      index  Index to retrieve
 *
 * @return     State of bit on requested index.
 */
static inline bool _bitset(uint8_t const * const data, uint16_t index)
{
    return (data[index / 8] & (1 << index % 8)) != 0;
}

/**
 * @brief      Finds the first set or clear pixel from given data 
 *
 * @param      data        Data to iterate over
 * @param      pixel_state State of the pixel, set/clear (true/false)
 * @param      index       Index to start searching from
 */
uint16_t Storage::findPixel(bool pixel_state, int16_t index)
{
    while(_bitset(bitmap.data, _translate(index)) != pixel_state)
    {
        ++index;
    }
    return index;
}

/**
 * @brief      Finds a scanline from the current bitmap and given row
 *
 * @param      row     Row to search for scanlines
 * @param      start   Starting pixel location will be stored here
 * @param      end     Ending pixel location will be stored here
 * @param      index   Index to start searching from
 */
void Storage::findScanline(int16_t row, int16_t &start, int16_t &end, int16_t index)
{
    // Read data of one row
    readMono40(row, 1);

    // Find start
    start = findPixel(true, index);
    // Find end
    end = findPixel(false, start + 1);
}

/**
 * @brief      Reads a monochrome bitmap of BITMAPINFOHEADER and compresses the data
 *
 * @param      row          Row where to start reading data
 * @param      amount       Amount of rows to read
 */
void Storage::readMono40(uint16_t row, uint16_t amount)
{
    uint32_t const width = _readWidth(this->file);
    uint8_t const offset = _readOffset(this->file);
    // Read and convert data
    uint16_t counter = 0; // Keep track of memory position
    delete[] this->bitmap.data;
    this->bitmap.data = new uint8_t [width / 8 + 1];
    // Calculate starting address
    this->file.seek(offset + row * (4 * (1 + width / 32)));

    // Read requested amount of data
    while(this->file.available() && counter < amount * width)
    {
        // Encoded as End-of-line
        // Read one line at a time
        uint16_t bytes_read = 0;
        if (width >= 8)
        {
            while (bytes_read < (width / 8))
            {
                this->bitmap.data[counter++] = this->file.read();
                bytes_read++;
            }
        }

        // Check if width is uneven and still contains leftover bits
        uint8_t leftover = width % 8;
        if (leftover > 0)
        {
            this->bitmap.data[counter++] = this->file.read();
            bytes_read++;
        }

        if (bytes_read % 4 > 0)
        {
            // Hop forward over padding to the position of the next line
            this->file.seek(this->file.position() + 4 - (bytes_read % 4));
        }
    }
}

/**
 * @brief      Encodes the bitmap into scanline format
 *
 * @param      filename_original  Filename of original bitmap
 * @param      filename_encoded   Filename for the encoded bitmap
 */
void Storage::encodeBitmap(char filename_original[], char filename_encoded[])
{
    Serial.print("Encoding: ");
    Serial.println(filename_original);

    for (uint16_t row = 0; row  < bitmap.height; ++row)
    {
        int16_t start = 0, end = 0, offset = 0;
        while (start < bitmap.width)
        {
            findScanline(row, start, end, offset);
            if (start < end && start < bitmap.width && end <= bitmap.width)
            {
                writeCompressedBlock(filename_encoded, row, start, end);
                fileOpenToRead(filename_original);
            }
            offset = end + 1;
        }
    }

    Serial.println("...done encoding!");
}

/**
 * @brief      Reads a bitmap from a file
 *
 * @param      row          Row where to start reading data
 * @param      amount       Amount of rows to read
 */
void Storage::readBitmap(uint16_t row, uint16_t amount)
{
    uint32_t const width = _readWidth(this->file);
    uint32_t const height = _readHeight(this->file);
    uint8_t const bits_per_pixel = _readBitsPerPixel(this->file);
    uint8_t const compression_method = _readCompressionMethod(this->file);
    uint8_t const offset = _readOffset(this->file);

    bitmap.width = width;
    bitmap.height = height;

    // Check if we know how to parse the format
    if (bits_per_pixel == 1 && compression_method == 0)
    {
        // Serial.println("Monochrome BITMAPINFOHEADER bitmap identified");
        // Check if encoded version already exists
        char filename_encoded[30];
        char extension[5] = ".cbm";
        char filename_original[30];
        this->file.getName(filename_original, 30);

        strcpy(filename_encoded, filename_original);
        strtok(filename_encoded, ".");
        strcat(filename_encoded, extension);

        char filepath_encoded[50] = "/enc/";
        strcat(filepath_encoded, filename_encoded);

        if (SD.exists(filepath_encoded))
        {
            return;
        }

        if (!SD.exists("/enc"))
        {
            SD.mkdir("enc");
        }
        encodeBitmap(filename_original, filepath_encoded);
    }
    else
    {
        Serial.println("Bitmap of unknown format! Unable to parse!");
    }
}

/**
 * @brief      Gets bitmap data from an image on the SD card.
 *
 * @param      filepath   Filepath to read from.
 * @param      row        Row where to start reading data
 * @param      amount     Amount of pixels to read.
 *
 * @return     Bitmap containing the data or an empty struct if failed
 */
Bitmap const &Storage::getBitmap(char filepath[], uint16_t row, uint16_t amount)
{
    if (!this->fileOpenToRead(filepath))
    {
        this->bitmap.width = -1;
        this->bitmap.height = -1;
        this->bitmap.type = BitmapType::file_error;
        return this->bitmap;
    }

    readBitmap(row, amount);
    return this->bitmap;
}

/**
 * @brief      Opens a file for reading if it hasn't yet been opened
 *
 * @param      filepath  Filepath of the file
 *
 * @return     True on success, False otherwise
 */
bool Storage::fileOpenToRead(char filepath[])
{
    char filepath_open[20];
    this->file.getName(filepath_open, 20);

    if (strcmp(filepath_open, filepath) == 0)
    {
        // Same file as previously
        // Therefore, it should already be open
        // Just seek to beginning because of new request to open
        this->file.seek(0);
        return true;
    }

    // Different file than previously
    // Close previous one and open a new one
    this->file.close();
    this->file = SD.open(filepath);
    if (!this->file)
    {
        Serial.print("Failed to open file: ");
        Serial.println(filepath);
        return false;
    }
    // Serial.print("Opening file: ");
    // Serial.println(filepath);
    return true;
}

/**
 * @brief      Opens a file for writing
 *
 * @param      filepath  Filepath of the file
 * @param      overwrite Overwrite file if it exists
 *
 * @return     True on success, False otherwise
 */
bool Storage::fileOpenToWrite(char filepath[], bool overwrite)
{
    if (SD.exists(filepath) && overwrite)
    {
        SD.remove(filepath);
    }

    // Close previous one and open a new one
    this->file.close();
    this->file = SD.open(filepath, FILE_WRITE);
    if (!this->file)
    {
        Serial.print("Failed to open file: ");
        Serial.println(filepath);
        return false;
    }
    // Serial.print("Opening file: ");
    // Serial.println(filepath);
    return true;
}

/**
 * @brief      Returns size of currently opened file
 *
 * @return     Amount of bytes available
 */
uint32_t Storage::fileSize()
{
    if (!this->file)
    {
        // Serial.println("No file was open for reporting size!");
        return false;
    }

    return this->file.size();
}

/**
 * @brief      Reads requested amount of data from a file
 *
 * @param      data    Buffer to read into
 * @param      amount  Amount of bytes to read. Will read less if requested amount not available.
 *
 * @return     Amount of bytes read, -1 on failure.
 */
int32_t Storage::fileReadData(uint8_t data[], uint16_t amount)
{
    if (!this->file)
    {
        // Serial.println("No file was open for reading!");
        return -1;
    }

    uint32_t available = this->file.available();
    if (amount > available)
    {
        // Not enough data left on file
        this->file.read(data, available);
        return available;
    }

    // Read normally
    this->file.read(data, amount);
    return amount;
}

/**
 * @brief      Writes data into a file
 *
 * @param      data    Array of data to write
 * @param      amount  Amount of bytes to write
 *
 * @return     Amount of bytes written, -1 on failure.
 */
int32_t Storage::fileWriteData(uint8_t data[], uint16_t amount)
{
    if (!this->file)
    {
        // Serial.println("No file was open for writing!");
        return -1;
    }

    return this->file.write(data, amount);
}

/**
 * @brief      Closes the file handle after writing has finished
 *
 * @return     True on success, False otherwise
 */
bool Storage::fileClose()
{
    // This method is not needed per se as the file is closed anyways when opening a new one
    // However, this is more secure in case of failures before opening another file etc.
    if (!this->file)
    {
        // Serial.println("No file was open for closing!");
        return false;
    }

    this->file.close();
    return true;
}

/**
 * @brief      Copies a file from one location to another
 *
 * @param      source     The source filepath
 * @param      dest       The destination filepath
 * @param      overwrite  Overwrite existing file
 *
 * @return     True on success, False otherwise
 */
bool Storage::fileCopy(char source[], char dest[], bool overwrite)
{
    if (SD.exists(dest))
    {
        if (overwrite)
        {
            SD.remove(dest);
        }
        else
        {
            // Can't overwrite existing
            return false;
        }
    }

    if (!fileOpenToRead(source))
    {
        // Failed to open source file
        return false;
    }

    // Copy file using a buffer
    uint16_t buffer_len = 64;
    uint8_t buffer[buffer_len];
    int16_t file_size = this->file.available();
    for (int16_t i = 0; i * buffer_len < file_size; ++i)
    {
        fileOpenToRead(source);
        this->file.seek(i * buffer_len);
        uint16_t read_amount = fileReadData(buffer, buffer_len);
        fileOpenToWrite(dest, false);
        fileWriteData(buffer, read_amount);
    }
    fileClose();
}

/**
 * @brief      Gets the previous file in the filesystem
 *
 * Note that this function is unsafe and will loop forever with a bad input argument
 *
 * @param      filename_current  Filename of the current file
 *
 * @return     The previous file
 */
File Storage::fileGetPrevious(char filename_current[])
{
    // Start looping over all of the files
    File root = SD.open("enc");
    File entry;

    char filename_previous[30];
    char filename_entry[30];

    // Find the position of the current file
    while (strcmp(filename_current, filename_entry) != 0)
    {
        strcpy(filename_previous, filename_entry);

        entry = root.openNextFile();
        entry.getName(filename_entry, 30);
    }

    char filepath[30] = "/enc/";
    strcat(filepath, filename_previous);
    entry = SD.open(filepath);
    root.close();
    return entry;
}

/**
 * @brief      Gets the next file in the filesystem
 *
 * Note that this function is unsafe and will loop forever with a bad input argument
 *
 * @param      filename_current  Filename of the current file
 *
 * @return     The next file
 */
File Storage::fileGetNext(char filename_current[])
{
    // Start looping over all of the files
    File root = SD.open("enc");
    File entry;

    char filename_entry[30];

    // Find the position of the current file
    while (strcmp(filename_current, filename_entry) != 0)
    {
        entry = root.openNextFile();
        entry.getName(filename_entry, 30);
    }

    char filepath[30] = "/enc/";
    entry = root.openNextFile();
    entry.getName(filename_current, 30);
    strcat(filepath, filename_current);
    entry = SD.open(filepath);
    root.close();
    return entry;
}

/**
 * @brief      Load mono_color from SD card
 *
 * @return     Value of Display::mono_color
 */
uint16_t Storage::fileGetMonoColor()
{
    char filename[20] = "monocolor";
    uint16_t mono_color = 0xF800;
    if (fileOpenToRead(filename))
    {
        fileReadData((uint8_t *)&mono_color, 2);
        fileClose();
    }
    return mono_color;
}

/**
 * @brief      Saves the color used by the Display
 *
 * @param      mono_color  Color to save
 */
void Storage::fileSaveMonoColor(uint16_t mono_color)
{
    char filename[20] = "monocolor";
    if (fileOpenToWrite(filename, true) && fileWriteData((uint8_t *)&mono_color, 2) == 2)
    {
        fileClose();
    }
    else
    {
        Serial.println("Failed to save color!");
    }
}

/**
* @brief      Fetches the mapping between floors and bitmaps
*
* @param      mapFileName  Name of the .txt file containing the mapping
*
* @return     MappingList containing the data or an empty struct if failed
*/
MappingList const& Storage::getMappingList(char mapFileName[])
{
    // Serial.print("Fetching mapping list...\n");
    if (!this->fileOpenToRead(mapFileName))
    {
        Serial.print("Could not fetch mapping list.\n");
        return this->mappinglist;
    }
    else
    {
        this->fileOpenToRead(mapFileName);

        char c;
        char floor_no[3] = "$";
        char bmp_name[20] = "$";
        char bmp_name2[20] = "$";

        bool comma1_reached = false;
        bool comma2_reached = false;

        uint32_t ctr = 0;
        uint32_t list_itr = 0;
        uint32_t mapped_floor_count = 0;

        do
        {
            c = this->file.read();
            if (c == '$') //indicates EOF
                break;

            if (c == ',' && !comma1_reached && !comma2_reached) //separates floor number from bitmap names
            {
                floor_no[ctr] = '\0';
                strcpy(this->mappinglist.map_list[list_itr].floorNo, floor_no);
                comma1_reached = true;
                ctr = 0;
            }
            else if (c == ',' && comma1_reached && !comma2_reached) //separates first bitmap name from second bitmap name
            {
                bmp_name[ctr] = '\0';
                strcpy(this->mappinglist.map_list[list_itr].bitmapName, bmp_name);
                comma2_reached = true;
                ctr = 0;
            }
            else if (c == '\n') //indicates end of a mapping
            {
                bmp_name2[ctr] = '\0';
                strcpy(this->mappinglist.map_list[list_itr].bitmapName2, bmp_name2);
                comma1_reached = false;
                comma2_reached = false;
                ctr = 0;
                if (strcmp(bmp_name, "\0") || strcmp(bmp_name2, "\0"))
                    mapped_floor_count++;
                list_itr++;
            }

            if (!comma1_reached && !comma2_reached && c != '\n') //the program is still reading the floor number character by character
                floor_no[ctr++] = c;
            else if (comma1_reached && !comma2_reached && c != ',') //the program is still reading the first bitmap name
                bmp_name[ctr++] = c;
            else if (comma1_reached && comma2_reached && c != ',') //the program is still reading the second bitmap name
                bmp_name2[ctr++] = c;
        } while (1);

        this->mappinglist.n_mapped_floors = mapped_floor_count;
        this->file.close();
        // Serial.print("Mapping list fetched successfully.\n");
        return this->mappinglist;
    }
}

/**
* @brief      Commits the mappinglist structure to mapping.ini file
*
* @param      mapFileName  Name of the .txt file to hold the mapping
*
* @return     0 if successful, -1 otherwise
*/
int Storage::commitMappingList(char mapFileName[]) //Needs to be called in the destructor of the Storage class, commits all changes to "mapping.ini" file for reuse
{
    Serial.print("Saving MappingList...\n");
    if (!this->fileOpenToWrite(mapFileName, true))
    {
        return -1;
    }
    else
    {
        uint32_t ctr = 0;
        for (ctr = 0; ctr <= 31; ctr++)
        {
            String _str_floorNo(this->mappinglist.map_list[ctr].floorNo);
            String _str_bitmapName(this->mappinglist.map_list[ctr].bitmapName);
            String _str_bitmapName2(this->mappinglist.map_list[ctr].bitmapName2);
            this->file.print(_str_floorNo + ',' + _str_bitmapName + ',' + _str_bitmapName2 + '\n'); //Commit ith mapping to file
        }
        this->file.print("$\n"); //EOF
        this->file.close();
        Serial.println("MappingList saved.");
        return 0;
    }
}

/**
* @brief      Updates the mapping for the specified floorNo with the specified bitmapName
*
* @param      floorNo  Char array specifying floorNo to be mapped
* @param      bitmapName  Char array specifying first bitmapName to map to floor
* @param      bitmapName2  Char array specifying second bitmapName to map to floor
*
* @return     0 if successful, -1 otherwise
*/
int Storage::setFloorMapping(char floorNo[], char bitmapName[], char bitmapName2[])
{
    uint32_t loc = findFromFloorNo(floorNo);
    if (loc == -1)
    {
        Serial.println("Specified floor does not exist.");
        return -1;
    }
    else
    {
        strcpy(this->mappinglist.map_list[loc].bitmapName, bitmapName);
        strcpy(this->mappinglist.map_list[loc].bitmapName2, bitmapName2);
        // Serial.print("Floor mapping set successfully.\n");
        return 0;
    }
}

/**
* @brief      Clears the mapping for the specified floorNo
*
* @param      floorNo  Char array specifying floorNo whose mapping is to be cleared
*
* @return     0 if successful, -1 otherwise
*/
int Storage::removeFloorMapping(char floorNo[])
{
    uint32_t loc = findFromFloorNo(floorNo);
    if (loc == -1)
    {
        Serial.println("Specified floor does not exist.");
        return -1;
    }
    else
    {
        strcpy(this->mappinglist.map_list[loc].bitmapName, "\0");
        strcpy(this->mappinglist.map_list[loc].bitmapName2, "\0");
        // Serial.print("Floor mapping removed successfully.\n");
        return 0;
    }
}

/**
* @brief      Finds the mapping list index corresponding to the mapping containing the specified floorNo
*
* @param      floorNo  Char array specifying floorNo to search for
*
* @return     index corresponding to the mapping containing the specified floorNo if found, -1 otherwise
*/
int Storage::findFromFloorNo(char floorNo[])
{
    uint32_t ctr;
    for (ctr = 0; ctr <= 31; ctr++)
    {
        if (!strcmp(this->mappinglist.map_list[ctr].floorNo, floorNo))
            break;
    }

    if (ctr == 32)
    {
        return -1;
    }
    else
    {
        return ctr;
    }
}

/**
* @brief      Finds the mapping list index corresponding to the mapping containing the specified bitmapName
*
* @param      bitmapName  Char array specifying bitmapName to search for
*
* @return     index corresponding to the mapping containing the specified bitmapName if found, -1 otherwise
*/
int Storage::findFromBitmapName(char bitmapName[], char bitmapName2[])
{
    uint32_t ctr;
    for (ctr = 0; ctr <= 31; ctr++)
    {
        if (!strcmp(this->mappinglist.map_list[ctr].bitmapName, bitmapName) && !strcmp(this->mappinglist.map_list[ctr].bitmapName2, bitmapName2))
            break;
    }

    if (ctr == 32)
    {
        return -1;
    }
    else
    {
        return ctr;
    }
}

/**
* @brief      Initializes a blank mapping file containing 32 entries (corresponding to 32 floors)
*
* @return     0 if successful, -1 otherwise
*/
int Storage::initMappingList(char mapFileName[])
{
    Serial.print("Initializing mapping list...\n");
    if (!this->fileOpenToWrite(mapFileName, true))
    {
        return -1;
    }
    else
    {
        uint32_t ctr = 0;
        for (ctr = 0; ctr <= 31; ctr++)
        {
            String _str_floorNo = String(ctr + 1);
            String _str_bitmapName = "";
            String _str_bitmapName2 = "";
            this->file.println(_str_floorNo + ',' + _str_bitmapName + ',' + _str_bitmapName2); //Commit ith mapping to file
        }
        this->file.println("$"); //EOF
        this->file.close();
        Serial.print("Mapping list initialized successfully.\n");
        return 0;
    }
}

/**
* @brief      Updates the mapping for the specified floorNo with the specified bitmapName
*
* @param      floorNo  Char array specifying floorNo to be mapped
* @param      bmp1  Pointer to char array that will hold first bitmap name
* @param      bmp2  Pointer to char array that will hold second bitmap name
*
* @return     0 if successful, -1 otherwise
*/
int Storage::getFloorMapping(char floorNo[], char bitmapName[], char bitmapName2[])
{
    uint32_t loc = findFromFloorNo(floorNo);
    if (loc == -1)
    {
        Serial.print("Specified floor does not exist.\n");
        return -1;
    }
    else
    {
        strcpy(bitmapName, this->mappinglist.map_list[loc].bitmapName);
        strcpy(bitmapName2, this->mappinglist.map_list[loc].bitmapName2);
        // Serial.print("Floor mapping retrieved successfully.\n");
        return 0;
    }
}
