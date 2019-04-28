#include "Network.h"
#include "Storage.h"

Network::Network()
{
    // Initialize
    // Set default values for private members
    // adress and name of one module: +ADDR:98d3:34:910f91, name: +NAME:HC-05
}


/**
 * @brief      Download file from currently connected node.
 *
 * @param      filepath The filename/path of the file
 * @param      push 1 = download pushed file from remote node, 0 = download normal file after requesting it from remote node
 */
void Network::downloadFile(char filepath[], bool push)
{
    // If routine download request (not servicing a push request)
    // Send download command to node
    // Send filepath to node
    if (!push) {
        Serial1.write(DOWNLOAD_FILE);
        Serial1.print(filepath);
    }

    // Let remote node know that you are ready
    Serial1.write(READY);
	Serial.print("Beginning download...\n");
    // Wait for remote node to send you file size
    // Receive how many bytes the file is going to be
	// Serial1 for bluetooth (arduino ports 18,19)
    while (Serial1.available() <= 0) // Could add a timer here to enforce a timeout
    {
        1;
    }
	uint32_t size_of_file = Serial1.read();

    // Start saving the received bytes to the SD card with the filename
    if (!Storage::instance().fileOpenToWrite(filepath, true)) // true = overwrite existing
    {
        Serial.println("Failed to open the file for writing!");
        Serial.println("Aborting download procedure!");
        return;
    }

    // Start saving the file to the SD card	
	uint8_t buffer[200];
	
    for (uint32_t i = 0; i < size_of_file;)
    {
        // Send 'READY' message to remote node
        Serial1.write(READY);

        // Wait for the remote node to respond
        while (Serial1.available() <= 0) // Could add a timer here to enforce a timeout
        {
            1;
        }

        //Receive 200 bytes from the node
        //buffer should be filled with the 200 bytes
		    
        for (int z=0; z<200; z++){
  			buffer[z]=Serial1.read();
        }

        // Save the 200 bytes to the SD card
        if ((size_of_file - i) < 200)
           Storage::instance().fileWriteData(buffer, size_of_file - i);
        else
           Storage::instance().fileWriteData(buffer, 200);

        // Written the current i bytes of the file, so advance counter by as much
        i += 200;

	}

	Serial.print("Download completed successfully...\n");

	
    // Finally close the file handle
    Storage::instance().fileClose();
}

/**
 * @brief      Upload file to currently connected node.
 *
 * @param      filepath The filename/path of the file
 * @param      push 1 = push upload file to remote node, 0 = upload file to remote node upon receiving download request
 */
void Network::uploadFile(char filepath[], bool push)
{
    uint8_t msg = '$';

    // Open the file and get size
    if (!Storage::instance().fileOpenToRead(filepath))
    {
        Serial.println("Failed to open file for uploading!");
        Serial.println("Aborting upload procedure!");
        return;
    }
    uint32_t size_of_file = Storage::instance().fileSize();
	
    // Send upload command to node with the file size so that it knows how many bytes to save
    if (push) { //if file is to be pushed, also send filepath first
        Serial1.write(UPLOAD_FILE);
        Serial1.print(filepath);
    }

	Serial.print("Waiting for 'READY'\n");
	
    // Wait for remote node to respond with 'READY' and then transmit file size
    while (Serial1.available() <= 0)  // Could add a timer here to enforce a timeout
    {
        1;
    }
    msg = Serial1.read();
	
	Serial.print("Message received: " + msg + '\n');
	
    if (msg == READY){
        Serial1.print(size_of_file);
		Serial.print("Size of file being sent is: " + size_of_file + '\n');
    }
	else {
        Serial.println("Remote node did not respond with 'READY'!");
        Serial.println("Aborting upload procedure!");
        return;
    }
    // Start sending data
    uint8_t buffer[200];

    for (uint32_t i = 0; i < size_of_file;) // Increment i by 200 due to sending 200 bytes at a time
    {
        // Wait for 'READY' message from remote node
        msg = '$';
        while (Serial1.available() <= 0)    // Could add a timer here to enforce a timeout
        {
            1;
        }
        msg = Serial1.read();
        if (msg == READY);
        {
            // Read 200 bytes from Storage
            Storage::instance().fileReadData(buffer, 200);

            // Send the 200 bytes
            for (int z = 0; z < 200; z++) {
                Serial1.write(buffer[z]);
            }

            // Read and sent the current i bytes of the file, so advance counter by as much
            i += 200;
        }
    }

    // Finally close the file handle
    Storage::instance().fileClose();
}

