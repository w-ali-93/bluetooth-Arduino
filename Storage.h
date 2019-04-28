#ifndef Storage_h
#define Storage_h

#include <SdFat.h>

#include <stdint.h>

typedef enum e_bitmap_type
{
    file_error = -1,
    error = 0,
    monochrome = 1,
    monochrome_compressed = 2,
    rgb888 = 3,
    rgb888_compressed = 4
} BitmapType;

typedef struct s_bitmap
{
    int32_t width = -1;
    int32_t height = -1;
    BitmapType type = BitmapType::file_error;
    union {
        uint16_t *pixels;
        uint8_t *data;
    };
} Bitmap;

typedef struct s_mapping
{
    // Structure holding a single mapping between bitmap and floor number
    char floorNo[3];
    char bitmapName[20];
	char bitmapName2[20];
} Mapping;

typedef struct s_mapping_list
{
    // Structure holding a list of mappings, plus some misc. info.
    int32_t n_mapped_floors = -1;
    Mapping map_list[32]; //32 maximum floors
} MappingList;

class Storage
{
public:
    // Public getter for Storage instance
    static Storage& instance()
    {
        static Storage storage;
        return storage;
    }
    // Prohibit copying of Storage class
    Storage(Storage const&)        = delete;
    void operator=(Storage const&) = delete;

    // Bitmap functions
    Bitmap const& getBitmap(char filepath[], uint16_t row, uint16_t amount);
    // File functions
    bool fileOpenToRead(char filepath[]);
    bool fileOpenToWrite(char filepath[], bool overwrite=false);
    uint32_t fileSize();
    int32_t fileReadData(uint8_t buffer[], uint16_t amount);
    int32_t fileWriteData(uint8_t data[], uint16_t amount);
    bool fileClose();
    bool fileCopy(char source[], char dest[], bool overwrite=false);
    // Browsing functions
    File fileGetPrevious(char filename_current[]);
    File fileGetNext(char filepath_current[]);
    // Other file saving convenience functions
    uint16_t fileGetMonoColor();
    void fileSaveMonoColor(uint16_t mono_color);
    // Floor mapping functions
    MappingList const& getMappingList(char mapFileName[]); //Read data from "data\mapping.ini" into MappingList structure
    int commitMappingList(char mapFileName[]); //Commit current MappingList structure to "data\mapping.ini"
    int setFloorMapping(char floorNo[], char bitmapName[], char bitmapName2[]); //Set mapping between specified parameters in MappingList structure
    int removeFloorMapping(char floorNo[]); //Remove mapping between specified parameters in MappingList structure 
    int getFloorMapping(char floorNo[], char bitmapName[], char bitmapName2[]); //Get mapping for indicated floors, stored in bitmapName and bitmapName2 arrays
    int initMappingList(char mapFileName[]); //Initialize a blank mapping file containing 32 entries (corresponding to 32 floors)
    
private:
    // Don't allow any external parties to construct a Storage instance
    Storage();
    void readMono40(uint16_t row, uint16_t amount);
    void readBitmap(uint16_t row, uint16_t amount);
    uint16_t findPixel(bool pixel_state, int16_t offset);
    void findScanline(int16_t row, int16_t &start, int16_t &end, int16_t offset=-1);
    void writeCompressedBlock(char filename[], uint16_t row, uint16_t start, uint16_t end);
    void encodeBitmap(char filename_original[], char filename_encoded[]);
	int findFromFloorNo(char floorNo[]); //Return the index corresponding to mapping containing the specified floorNo
	int findFromBitmapName(char bitmapName[], char bitmapName2[]); //Return the index corresponding to mapping containing the specified bitmapName
    /** @brief File handle to use internally */
    File file;
    /** @brief Pointer to dynamically allocated data of last read bitmap */
    Bitmap bitmap;
    /** @brief Pointer to dynamically allocated data of last read mapping file */
    MappingList mappinglist;
};

#endif
