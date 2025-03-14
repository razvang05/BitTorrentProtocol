# BitTorrent Protocol Simulation using MPI

### **Name**: Gheorghe Marius Razvan  
### **Group**: 334CA  
### **Date**: 7.01.2025
### **Email**: rzvrazvan03@gmail.com

## **Description**:
This project simulates the BitTorrent protocol using MPI, enabling file sharing through a decentralized peer-to-peer system. The protocol is implemented with multiple clients and a tracker that facilitate the distribution of file segments among peers. The clients download missing chunks of files and upload them to other clients as well.

---

## **Overview of BitTorrent Protocol**:
BitTorrent is a decentralized peer-to-peer file-sharing protocol that distributes files in chunks. Clients (peers) can download and upload file chunks simultaneously from/to other peers, reducing the load on a single server. Each file is split into segments, and each client is responsible for sharing the segments it possesses while downloading others' segments.

---

## **Roles in BitTorrent**:
1. **Seed**: A client that holds the complete file and only uploads it to other peers.
2. **Peer**: A client that holds some segments of the file. It uploads and downloads segments.
3. **Leecher**: A client that only downloads segments and does not upload.

---

## **Tracker Functionality**:
The **tracker** coordinates the communication between peers:
- It stores information about file segments and the peers who hold them.
- It responds to client requests, providing them with the swarm (list of peers) for each file.
- It marks clients as **seeds** or **peers** and tracks their download status.

---

## **Client Functionality**:
### **1. Download Thread**:
The download thread handles the downloading of file segments. It works in coordination with the tracker to find and request missing chunks from other peers. The **download_thread_func()** function:
- Reads the client's input file and sends initial information to the tracker about the file segments the client holds.
- It interacts with the tracker to get information about the swarm (available peers) and file chunks.
- Downloads missing file segments using a peer selection strategy based on usage, ensuring a balanced load across peers.
- Once all chunks are downloaded, the client notifies the tracker and saves the complete file to disk.

### **2. Upload Thread**:
The upload thread is responsible for answering chunk requests from other clients. It allows the client to upload segments of the files it holds. The **upload_thread_func()** function:
- Runs concurrently with the download thread.
- Responds to chunk requests from peers by checking if the client has the requested chunk and sending it if available.
- The upload thread ensures the client continues to function as a seed (uploading chunks) while it may also be downloading chunks.

---

## **Key Functions**:

### **1. `tracker()`**:  
The tracker is the central point for coordination. It maintains records of files and peers in the system. This function:
- Receives the initial information from clients about the files they own and the chunks they have.
- Sends ACK to all clients once it has received the necessary data.
- Responds to swarm requests (`TAG_SWARM_REQ`), providing peers and hashes for specific files.
- Tracks file completion and sends notifications when a client finishes downloading (`TAG_FILE_COMPLETE`) or has completed all downloads (`TAG_ALL_COMPLETE`).

### **2. `peer()`**:  
This function runs the two main threads for each client (download and upload). It:
- Initializes and starts both the **download** and **upload** threads.
- Handles the coordination between the download and upload processes, ensuring that both threads work in parallel.
- Waits for the download thread to finish and then stops the upload thread when all downloads are complete.

### **3. `download_thread_func()`**:  
This function is executed by the **download thread**. It:
- Reads the input file to understand which files the client owns and which files it wants to download.
- Requests missing chunks from the tracker and attempts to download them from other peers.
- Uses a peer selection strategy based on usage count, aiming to balance the load across peers.
- Sends a request to the tracker every 10 chunks to update the list of peers.
- Once all chunks of a file are downloaded, the client notifies the tracker and saves the file locally.

### **4. `upload_thread_func()`**:  
This function is executed by the **upload thread**. It:
- Listens for chunk requests from other clients.
- If a request for a chunk is received, it checks if the client has the chunk and sends it if available.
- Operates concurrently with the download thread, allowing the client to both upload and download files simultaneously.

### **5. `save_completed_file()`**:  
This function is called once a client has completed downloading all chunks of a file. It:
- Saves the file to disk with the name `client<R>_<filename>.txt`, where `R` is the client's rank and `<filename>` is the name of the file.
- The file consists of the hashes of the downloaded chunks.

### **6. `handle_swarm_request()`**:  
Handles requests from clients for swarm information:
- The client sends a request to the tracker for the swarm (list of peers) for a specific file.
- The tracker responds with the list of peers and the file's chunk hashes.
- The client updates its list of peers and starts downloading the required chunks.

### **7. `handle_chunk_request()`**:  
Handles chunk requests from other peers:
- When a peer requests a chunk, the client checks if it owns the chunk.
- If the chunk is available, it sends an "OK" response. Otherwise, it responds with "NO".

---

## **Efficiency Considerations**:
- **Load Balancing**: To ensure a balanced distribution of download load, the client avoids downloading all chunks from a single peer. The client uses the `peer_usage_count` map to track how many times each peer has been used for downloading chunks, and prefers peers with fewer requests.
- **Mutex Synchronization**: Since both the download and upload threads access the `owned_files_local` data structure, mutex locks are used to prevent concurrent access and race conditions.

---

## **File System Details**:
1. **Input File Format**:
   Each client input file `in<R>.txt` should contain:
   - The number of files owned by the client.
   - The file name, number of chunks, and the hashes of each chunk.
   - The number of files the client wants to download, followed by the names of the desired files.

2. **Output File**:
   After downloading, each client saves the downloaded file as `client<R>_<filename>.txt`, containing the hashes of the segments it has downloaded.

---

## **Notable Requirements**:
- **Protocol Compliance**: The implementation must follow the BitTorrent protocol as described.
- **Efficiency**: The downloading process should be balanced across available peers.
- **Completion**: The client must notify the tracker when it has finished downloading the files.

---

## **How to Run**:
1. **Compile** and **Run** the project:
   ```bash
   mpirun -np <N> ./tema2
