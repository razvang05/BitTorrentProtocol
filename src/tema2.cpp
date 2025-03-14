#include <mpi.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <map>
#include <vector>
#include <string>
#include <algorithm>
using namespace std;

#define TRACKER_RANK 0
#define MAX_FILES 10
#define MAX_FILENAME 15
#define HASH_SIZE 32
#define MAX_CHUNKS 100

#define TAG_INIT_FILES     0  // client -> tracker: init
#define TAG_INIT_ACK       1  // tracker -> client: ack after init
#define TAG_SWARM_REQ      2  // client -> tracker: request swarm
#define TAG_SWARM_CHUNKS   3  // tracker -> client: answer number of chunks
#define TAG_SWARM_HASH     4  // tracker -> client: answer hashes
#define TAG_SWARM_PEERS    5  // tracker -> client: answer peers
#define TAG_CHUNK_REQ      6  // client -> client: request chunk
#define TAG_CHUNK_RESP     7  // client -> client: answer chunk
#define TAG_FILE_COMPLETE  8  // client -> tracker: file complete
#define TAG_ALL_COMPLETE   9  // client -> tracker: all complete
#define TAG_STOP          10  // tracker -> client: stop

map<string, vector<string>> owned_files_local;
pthread_mutex_t mutex = PTHREAD_MUTEX_INITIALIZER;
int downloading = 1;

void read_file(FILE *file, map<string, vector<string>>& owned_files_local) {

    // read number of files
    int num_owned_files;
    fscanf(file, "%d", &num_owned_files);

    MPI_Send(&num_owned_files, 1, MPI_INT, TRACKER_RANK, TAG_INIT_FILES, MPI_COMM_WORLD);

    for(int i = 0; i < num_owned_files; i++) {
        char curr_filename[MAX_FILENAME] = {0};
        int num_chunks;
        fscanf(file, "%s %d", curr_filename, &num_chunks);
        
        vector<string> hashes;

        // send filename and number of chunks
        MPI_Send(curr_filename, MAX_FILENAME, MPI_CHAR, TRACKER_RANK, TAG_INIT_FILES, MPI_COMM_WORLD);
        MPI_Send(&num_chunks, 1, MPI_INT, TRACKER_RANK, TAG_INIT_FILES, MPI_COMM_WORLD);

        // send each hash for the file
        for(int j = 0; j < num_chunks; j++) {
            char hash[HASH_SIZE + 1] = {0};
            fscanf(file, "%s", hash);
            hashes.push_back(string(hash));
            MPI_Send(hash, HASH_SIZE + 1, MPI_CHAR, TRACKER_RANK, TAG_INIT_FILES, MPI_COMM_WORLD);
        }
        // add file to local map
        owned_files_local[string(curr_filename)] = hashes;
    }

    // wait for ACK from tracker
    char ack[10];
    MPI_Recv(ack, 10, MPI_CHAR, TRACKER_RANK, TAG_INIT_ACK, MPI_COMM_WORLD, MPI_STATUS_IGNORE);

}

void read_wanted_files(FILE *file,
                      map<string, vector<string>>& wanted_files_local,
                      map<string,bool>& file_completed) {

    int nr_wanted_files;
    fscanf(file, "%d", &nr_wanted_files);

    for(int i = 0; i < nr_wanted_files; i++) {
        char wanted_filename[MAX_FILENAME] = {0}; 
        fscanf(file, "%s", wanted_filename);
        wanted_files_local[string(wanted_filename)] = vector<string>();
        file_completed[string(wanted_filename)] = false;
    }
}
void save_completed_file(int rank, const string& curr_file, const vector<string>& chunks) {
    char output_filename[50];
    sprintf(output_filename, "client%d_%s", rank, curr_file.c_str());
    FILE *out = fopen(output_filename, "w");
    for(const string& hash : chunks) {
        fprintf(out, "%s\n", hash.c_str());
    }
    fclose(out);
}

void try_get_chunk_from_peer(int peer, int chunk_idx, const char* curr_filename, const vector<string>& all_hashes) {
    // send chunk request to peer
    MPI_Send(curr_filename, MAX_FILENAME, MPI_CHAR, peer, TAG_CHUNK_REQ, MPI_COMM_WORLD);
    MPI_Send(&chunk_idx, 1, MPI_INT, peer, TAG_CHUNK_REQ, MPI_COMM_WORLD);
    MPI_Send(all_hashes[chunk_idx].c_str(), HASH_SIZE + 1, MPI_CHAR, peer, TAG_CHUNK_REQ, MPI_COMM_WORLD);
}

void *download_thread_func(void *arg) {
    int rank = *(int*)arg;
    char filename[20];
    sprintf(filename, "in%d.txt", rank);
    map<string, map<int, int>> peer_usage_count;
    
    FILE *file = fopen(filename, "r");
    if (!file) {
        printf("Client %d: Error open file %s\n", rank, filename);
        return NULL;
    }

    // read file and send to tracker
    read_file(file, owned_files_local);

    map<string, vector<string>> wanted_files_local;
    map<string, bool> file_completed;
    
    // read wanted files and store them in wanted_files_local
    read_wanted_files(file, wanted_files_local, file_completed);
    fclose(file);

    // process each wanted file
    for(auto& file_entry : wanted_files_local) {
        string curr_file = file_entry.first;
        vector<string>& my_chunks = file_entry.second;

        map<int, vector<int>> chunk_peers_tried;
        
        // while we don't have all chunks
        while(!file_completed[curr_file]) {
            
            char curr_filename[MAX_FILENAME] = {0};
            strncpy(curr_filename, curr_file.c_str(), MAX_FILENAME - 1);
            // send swarm request to tracker
            MPI_Send(curr_filename, MAX_FILENAME, MPI_CHAR, TRACKER_RANK, TAG_SWARM_REQ, MPI_COMM_WORLD);

            // receive number of peers and peers
            int num_peers;
            MPI_Recv(&num_peers, 1, MPI_INT, TRACKER_RANK, TAG_SWARM_PEERS, MPI_COMM_WORLD, MPI_STATUS_IGNORE);
            
            vector<int> peers(num_peers);
            for(int i = 0; i < num_peers; i++) {
                MPI_Recv(&peers[i], 1, MPI_INT, TRACKER_RANK, TAG_SWARM_PEERS, MPI_COMM_WORLD, MPI_STATUS_IGNORE);
            }

            // receive number of chunks and hashes
            int total_chunks;
            MPI_Recv(&total_chunks, 1, MPI_INT, TRACKER_RANK, TAG_SWARM_CHUNKS, MPI_COMM_WORLD, MPI_STATUS_IGNORE);
            
            vector<string> all_hashes(total_chunks);
            for(int i = 0; i < total_chunks; i++) {
                char hash[HASH_SIZE + 1];
                // store hash in all_hashes
                MPI_Recv(hash, HASH_SIZE + 1, MPI_CHAR, TRACKER_RANK, TAG_SWARM_HASH, MPI_COMM_WORLD, MPI_STATUS_IGNORE);
                all_hashes[i] = hash;
            }
            // set each chunk as not owned
            vector<bool> have_chunk(total_chunks, false);
            for(size_t i = 0; i < my_chunks.size(); i++) {
                for(int j = 0; j < total_chunks; j++) {
                    if(my_chunks[i] == all_hashes[j]) {
                        have_chunk[j] = true;
                    }
                }
            }

            // reser number of peers tried for each chunk
            // because meanwhile update list of peers
            chunk_peers_tried.clear();

            int downloaded_this_round = 0;
            // for each chunk we don't have yet 
            for(int i = 0; i < total_chunks && !file_completed[curr_file]; i++) {
                // if we don't have the chunk
                if(!have_chunk[i]) {
                    bool chunk_obtained = false;

                    // sort peers by usage count
                    vector<pair<int, int>> sorted_peers;
                    for(int peer : peers) {
                        sorted_peers.push_back({peer_usage_count[curr_file][peer], peer});
                    }
                    sort(sorted_peers.begin(), sorted_peers.end());

                    // for each peer in sorted order
                    for(const auto& peer_pair : sorted_peers) {
                        int peer = peer_pair.second;

                        // check if we have already tried this peer for this chunk
                        if(find(chunk_peers_tried[i].begin(), chunk_peers_tried[i].end(), peer) != chunk_peers_tried[i].end()) {
                            continue;
                        }
                        // add peer to list of peers tried for this chunk
                        chunk_peers_tried[i].push_back(peer);

                        // try to get chunk from peer
                        try_get_chunk_from_peer(peer,i,curr_filename,all_hashes);

                        char response[10];
                        MPI_Recv(response, 10, MPI_CHAR, peer, TAG_CHUNK_RESP, MPI_COMM_WORLD, MPI_STATUS_IGNORE);

                        if(strcmp(response, "NO") != 0) {
                            // increment usage count for peer for this file
                            peer_usage_count[curr_file][peer]++;
                            
                            // add chunk to my_chunks
                            my_chunks.push_back(all_hashes[i]);
                            // increment downloaded chunks
                            downloaded_this_round++;
                            have_chunk[i] = true;

                            pthread_mutex_lock(&mutex);
                            if(owned_files_local.find(curr_file) == owned_files_local.end()) {
                                owned_files_local[curr_file] = vector<string>();
                            }
                            // add chunk to owned_files_local
                            owned_files_local[curr_file].push_back(all_hashes[i]);
                            pthread_mutex_unlock(&mutex);

                            // check if we downloaded 10 chunks and request next swarm
                            if(downloaded_this_round == 10 || peer_usage_count[curr_file][peer] > total_chunks/2) {
                                break;
                            }
                            // exit and don't try other peers
                            chunk_obtained = true;
                            break;
                        }
                    }
                    // exit if we couldn't obtain the chunk from any peer and request swarm
                    if(!chunk_obtained) {
                        break;
                    }
                }
            }

            // if we have all chunks, send file complete
            if(my_chunks.size() == static_cast<size_t>(total_chunks)) {
                file_completed[curr_file] = true;
                MPI_Send(curr_filename, MAX_FILENAME, MPI_CHAR, TRACKER_RANK, TAG_FILE_COMPLETE, MPI_COMM_WORLD);

                // create file_otput with all chunks
                save_completed_file(rank, curr_file, my_chunks);
                break;
            } 
            else if(downloaded_this_round == 0) {
                break;
            }
        }
    }
    // send all complete message and finish
    MPI_Send(&rank, 1, MPI_INT, TRACKER_RANK, TAG_ALL_COMPLETE, MPI_COMM_WORLD);

    return NULL;
}

void receive_chunk_request(char* requested_file, int& chunk_idx, char* requested_hash, int& source, MPI_Status& status) {
    // receive name of file
    MPI_Recv(requested_file, MAX_FILENAME, MPI_CHAR, status.MPI_SOURCE, TAG_CHUNK_REQ, MPI_COMM_WORLD, &status);
    
    // receive chunk index
    MPI_Recv(&chunk_idx, 1, MPI_INT, status.MPI_SOURCE, TAG_CHUNK_REQ, MPI_COMM_WORLD, &status);
    
    // receive hash of chunk
    MPI_Recv(requested_hash, HASH_SIZE + 1, MPI_CHAR, status.MPI_SOURCE, TAG_CHUNK_REQ, MPI_COMM_WORLD, &status);
    
    // save source of request
    source = status.MPI_SOURCE;
}

void handle_chunk_request(const char* requested_file, int chunk_idx, const char* requested_hash, int source) {
    bool found = false;
    string hash_to_send;

    // block access to owned_files_local
    pthread_mutex_lock(&mutex);
    
    // check if we have the requested chunk
   if(owned_files_local.find(string(requested_file)) != owned_files_local.end() &&
    chunk_idx < (int)owned_files_local[string(requested_file)].size() &&
    owned_files_local[string(requested_file)][chunk_idx] == string(requested_hash)) {

        found = true;
    }
    pthread_mutex_unlock(&mutex);

    // send response to source else send NO
    const char* response = found ? "OK" : "NO";
    MPI_Send(response, strlen(response) + 1, MPI_CHAR, source, TAG_CHUNK_RESP, MPI_COMM_WORLD);
}

void *upload_thread_func(void *arg) {

    MPI_Status status;

    while(1) {
        // check if we should continue
        pthread_mutex_lock(&mutex);
        int should_continue = downloading;
        pthread_mutex_unlock(&mutex);

        if(!should_continue) {
            break;
        }

        int flag;
        // check if have any requests non-blocking
        MPI_Iprobe(MPI_ANY_SOURCE, TAG_CHUNK_REQ, MPI_COMM_WORLD, &flag, &status);

        if(flag) {
            // if we have a request, handle it
            char requested_file[MAX_FILENAME] = {0};
            int chunk_idx;
            char requested_hash[HASH_SIZE + 1];
            int source;

            receive_chunk_request(requested_file, chunk_idx, requested_hash, source, status);
            handle_chunk_request(requested_file, chunk_idx, requested_hash, source);

        }
    }

    return NULL;
}

void receive_client_info(int client, map<string, vector<string>>& file_segments, map<string, vector<int>>& file_owners) {
    int num_owned;
    // receive number of files
    MPI_Recv(&num_owned, 1, MPI_INT, client, TAG_INIT_FILES, MPI_COMM_WORLD, MPI_STATUS_IGNORE);

    for(int i = 0; i < num_owned; i++) {
        char filename[MAX_FILENAME];
        int num_chunks;
        // receive filename and number of chunks
        MPI_Recv(filename, MAX_FILENAME, MPI_CHAR, client, TAG_INIT_FILES, MPI_COMM_WORLD, MPI_STATUS_IGNORE);
        MPI_Recv(&num_chunks, 1, MPI_INT, client, TAG_INIT_FILES, MPI_COMM_WORLD, MPI_STATUS_IGNORE);

        // receive each hash for the file
        vector<string> hashes;
        for(int j = 0; j < num_chunks; j++) {
            char hash[HASH_SIZE + 1];
            MPI_Recv(hash, HASH_SIZE + 1, MPI_CHAR, client, TAG_INIT_FILES, MPI_COMM_WORLD, MPI_STATUS_IGNORE);
            hashes.push_back(string(hash));
        }

        string file_str(filename);
        if(file_segments.find(file_str) == file_segments.end()) {
            // add hashes to file segments
            file_segments[file_str] = hashes;
        }
        // add client to file owners
        file_owners[file_str].push_back(client);
    }
}

void handle_swarm_request(int source, map<string, vector<string>>& file_segments, map<string, vector<int>>& file_owners) {
    char filename[MAX_FILENAME];
    MPI_Recv(filename, MAX_FILENAME, MPI_CHAR, source, TAG_SWARM_REQ, MPI_COMM_WORLD, MPI_STATUS_IGNORE);
                
    string file_str(filename);
    // set current client as peer for the file if not already
    if(find(file_owners[file_str].begin(), file_owners[file_str].end(), source) == file_owners[file_str].end()) {
        file_owners[file_str].push_back(source);
    }    
    // send number of peers and peers
    vector<int>& owners = file_owners[file_str];
    int num_peers = owners.size();
    MPI_Send(&num_peers, 1, MPI_INT, source, TAG_SWARM_PEERS, MPI_COMM_WORLD);
    for(int peer : owners) {
        MPI_Send(&peer, 1, MPI_INT, source, TAG_SWARM_PEERS, MPI_COMM_WORLD);
    }

    // send number of hashes and hashes
    vector<string>& hashes = file_segments[file_str];
    int num_chunks = hashes.size();
    MPI_Send(&num_chunks, 1, MPI_INT, source, TAG_SWARM_CHUNKS, MPI_COMM_WORLD);
    for(const string& hash : hashes) {
        MPI_Send(hash.c_str(), HASH_SIZE + 1, MPI_CHAR, source, TAG_SWARM_HASH, MPI_COMM_WORLD);
    }
            
}

void handle_file_complete(int source, map<string, vector<int>>& file_owners) {
    char filename[MAX_FILENAME];
    MPI_Recv(filename, MAX_FILENAME, MPI_CHAR, source, TAG_FILE_COMPLETE, MPI_COMM_WORLD, MPI_STATUS_IGNORE);
                
    string file_str(filename);
    // set current client as seed for the file if not already
    if(find(file_owners[file_str].begin(), file_owners[file_str].end(), source) == file_owners[file_str].end()) {
        file_owners[file_str].push_back(source);
    }
}

bool handle_all_complete(int source, vector<bool>& client_finished, int& finished_count, int numtasks) {
    int client_rank;
    MPI_Recv(&client_rank, 1, MPI_INT, source, TAG_ALL_COMPLETE, MPI_COMM_WORLD, MPI_STATUS_IGNORE);
    
    // mark client as finished
    if(!client_finished[source]) {
        client_finished[source] = true;
        finished_count++;
        
        // check if all clients are finished
        if(finished_count == numtasks - 1) {
            int stop_msg = -1;
            // send stop message to all clients
            for(int client = 1; client < numtasks; client++) {
                MPI_Send(&stop_msg, 1, MPI_INT, client, TAG_STOP, MPI_COMM_WORLD);
            }
            return true;
        }
    }
    return false;
}

void tracker(int numtasks, int rank) {

    map<string, vector<string>> file_segments;
    map<string, vector<int>> file_owners;
    vector<bool> client_finished(numtasks, false);
    int finished_count = 0;

    // receive info from all clients
    for(int client = 1; client < numtasks; client++) {
        receive_client_info(client, file_segments, file_owners);
    }

    // send ACK to all clients
    const char* ack = "OK";
    for(int client = 1; client < numtasks; client++) {
        MPI_Send(ack, strlen(ack) + 1, MPI_CHAR, client, TAG_INIT_ACK, MPI_COMM_WORLD);
    }

    // loop until all clients are done
    bool all_done = false;
    while(!all_done) {
        MPI_Status status;
        int source;
        
        // receive any message from any client
        MPI_Probe(MPI_ANY_SOURCE, MPI_ANY_TAG, MPI_COMM_WORLD, &status);
        source = status.MPI_SOURCE;

        switch(status.MPI_TAG) {
            // swarm request
            case TAG_SWARM_REQ: 
                handle_swarm_request(source, file_segments, file_owners);
                break;

            // file complete
            case TAG_FILE_COMPLETE:
                handle_file_complete(source,file_owners);
                break;

            // all complete
            case TAG_ALL_COMPLETE:
                all_done = handle_all_complete(source, client_finished, finished_count, numtasks);
                break;
        }
    }
    
}

void peer(int numtasks, int rank) {
    pthread_t download_thread, upload_thread;
    void *status;
    int r;

    // Create threads
    r = pthread_create(&download_thread, NULL, download_thread_func, &rank);
    if(r) {
        printf("Eroare la crearea thread-ului de download\n");
        exit(-1);
    }
    
    r = pthread_create(&upload_thread, NULL, upload_thread_func, &rank);
    if(r) {
        printf("Eroare la crearea thread-ului de upload\n");
        exit(-1);
    }

    // wait for threads to finish
    r = pthread_join(download_thread, &status);
    if(r) {
        printf("Eroare la asteptarea thread-ului de download\n");
        exit(-1);
    }

    // receive stop message
    int stop_msg;
    MPI_Recv(&stop_msg, 1, MPI_INT, TRACKER_RANK, TAG_STOP, MPI_COMM_WORLD, MPI_STATUS_IGNORE);

    // stop upload thread
    pthread_mutex_lock(&mutex);
    downloading = 0;
    pthread_mutex_unlock(&mutex);

    r = pthread_join(upload_thread, &status);
    if(r) {
        printf("Eroare la asteptarea thread-ului de upload\n");
        exit(-1);
    }
}

int main(int argc, char *argv[]) {
    int numtasks, rank;
 
    int provided;
    MPI_Init_thread(&argc, &argv, MPI_THREAD_MULTIPLE, &provided);
    if (provided < MPI_THREAD_MULTIPLE) {
        fprintf(stderr, "MPI nu are suport pentru multi-threading\n");
        exit(-1);
    }
    MPI_Comm_size(MPI_COMM_WORLD, &numtasks);
    MPI_Comm_rank(MPI_COMM_WORLD, &rank);

    if(rank == TRACKER_RANK) {
        tracker(numtasks, rank);
    } else {
        peer(numtasks, rank);
    }

    MPI_Finalize();
    return 0;
}