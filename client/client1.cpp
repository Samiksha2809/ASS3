#include <bits/stdc++.h>
#include <arpa/inet.h> 
#include <sys/socket.h> 
#include <unistd.h>
#include <pthread.h>
#include <fcntl.h>
#include <openssl/sha.h>
using namespace std;

const int FILE_CHUNK_SIZE = 512 * 1024; // 512 KB

string client_ip, client_port;
unordered_map<string, unordered_map<string, bool>> isuploaded;
unordered_map<string, string> fileToFilePath; 
string tracker1Ip, tracker2Ip;
int tracker1Port, tracker2Port, peer_port, peer_ip;
bool loggedIn;



vector<string> splitIntoParts(const string& input, const string& delimiter = ":") {
    vector<string> parts;
    size_t position = 0;
    std::string remaining = input;

    while ((position = remaining.find(delimiter)) != string::npos) {
        string token = remaining.substr(0, position);
        parts.push_back(token);
        remaining.erase(0, position + delimiter.length());
    }
    parts.push_back(remaining);
    return parts;
}


// Function to compute the file size
long long file_size(const char* path) {
    FILE* fp = fopen(path, "rb");
    if (!fp) return -1;

    fseek(fp, 0, SEEK_END);
    long size = ftell(fp);
    fclose(fp);
    return size;
}

// Function to compute the SHA1 hash of a chunk
string computeChunkHash(const string& chunk) {
    unsigned char md[SHA_DIGEST_LENGTH];
    SHA1(reinterpret_cast<const unsigned char*>(chunk.data()), chunk.size(), md);
    
    // Convert to hex string
    string hash;
    for (int i = 0; i < SHA_DIGEST_LENGTH; i++) {
        char buf[3];
        sprintf(buf, "%02x", md[i]);
        hash += buf;
    }
    return hash;
}

// Function to upload a file
int uploadFile(vector<string> inpt, int sock) {
    if (inpt.size() != 3) {
        cout << "Invalid argument count" << endl;
        return 0;
    }

    const char* filepath = inpt[1].c_str();
    string filename = splitIntoParts(filepath, "/").back();

    if (isuploaded[inpt[2]].find(filename) != isuploaded[inpt[2]].end()) {
        cout << "File already uploaded" << endl;
        send(sock, "error", 5, MSG_NOSIGNAL);
        return 0;
    } else {
        isuploaded[inpt[2]][filename] = true;
        fileToFilePath[filename] = filepath;
    }

    // Prepare to read file in chunks
    FILE* file = fopen(filepath, "rb");
    if (!file) {
        cout << "Error opening file" << endl;
        return -1;
    }

    string piecewiseHash;
    char buffer[FILE_CHUNK_SIZE];
    while (true) {
        size_t bytesRead = fread(buffer, 1, FILE_CHUNK_SIZE, file);
        if (bytesRead == 0) break;

        string chunk(buffer, bytesRead);
        string chunkHash = computeChunkHash(chunk);
        piecewiseHash += chunkHash + "$$";
    }
    fclose(file);

    // Remove last "$$"
    if (!piecewiseHash.empty()) {
        piecewiseHash.pop_back();
        piecewiseHash.pop_back();
    }

    // Get file size and full hash
    string filehash = computeChunkHash(string(filepath)); // Use appropriate hashing for full file if needed
    string filesize = to_string(file_size(filepath));

    // Prepare file details
    string fileDetails = string(filepath) + "$$" +
                         string(client_ip) + ":" + string(client_port) + "$$" +
                         filesize + "$$" +
                         filehash + "$$" +
                         piecewiseHash;

    if (send(sock, fileDetails.c_str(), fileDetails.size(), MSG_NOSIGNAL) == -1) {
        cout << "Error sending file details: " << strerror(errno) << endl;
        return -1;
    }

    char server_reply[10240] = {0}; 
    read(sock, server_reply, sizeof(server_reply));
    cout << server_reply << endl;

    return 0;
}




vector<string> loadTrackerInfo(const string &filename) {
    vector<string> res;
    ifstream file(filename);
    string line;
    while (getline(file, line)) {
        cout << "Tracker info: " << line << endl;
        res.push_back(line);
    }
    return res;
}



int connectToTracker(int trackerNum, struct sockaddr_in &serv_addr, int sock) {
    char* curTrackIP;
    int curTrackPort;
    if(trackerNum == 1) {
        curTrackIP = &tracker1Ip[0]; 
        curTrackPort = tracker1Port;
    } else {
        curTrackIP = &tracker2Ip[0]; 
        curTrackPort = tracker2Port;
    }

    serv_addr.sin_family = AF_INET; 
    serv_addr.sin_port = htons(curTrackPort); 

    if(inet_pton(AF_INET, curTrackIP, &serv_addr.sin_addr) <= 0)  {
        return -1;
    }
    if (connect(sock, (struct sockaddr *)&serv_addr, sizeof(serv_addr)) < 0) { 
        return -1;
    }
    cout << "Connected to server " << curTrackPort << endl;
    return 0;
}



int main(int argc, char* argv[]) {
    if(argc != 3) {
        cout << "Provide arguments as <peer IP:port> and <tracker info file name>\n";
        return -1;
    }

    // Load tracker info
    string client_info = argv[1];
    string tracker_info = argv[2];
    vector<string> trackerAddress = loadTrackerInfo(tracker_info);

    vector<string> client_info_arr = splitIntoParts(client_info);
    client_ip = client_info_arr[0];
    client_port = client_info_arr[1];

    // Initialize tracker addresses
    tracker1Ip = trackerAddress[0];
    tracker1Port = stoi(trackerAddress[1]);
    tracker2Ip = trackerAddress[2];
    tracker2Port = stoi(trackerAddress[3]);

    // Socket setup
    int sock = 0; 
    struct sockaddr_in serv_addr; 

    if ((sock = socket(AF_INET, SOCK_STREAM, 0)) < 0) {  
        cout << "Socket creation error\n"; 
        return -1; 
    }
    cout << "Peer socket created\n";

    // Connect to tracker
    if (connectToTracker(1, serv_addr, sock) < 0) {
        cout << "Failed to connect to tracker\n";
        return -1; 
    }

    while (true) { 
        cout << ">> ";
        string inptline;
        getline(cin, inptline);

        if (inptline.empty()) continue;

        stringstream ss(inptline);
        vector<string> inpt;
        string s;
        while (ss >> s) {
            inpt.push_back(s);
        }

        // Handle commands
        if (inpt[0] == "login" && loggedIn) {
            cout << "There is already one active session\n";
            continue;
        }
        if (inpt[0] != "login" && inpt[0] != "create_user" && !loggedIn) {
            cout << "Please create account / login\n";
            continue;
        }

        // Send command to tracker
        if (send(sock, inptline.c_str(), inptline.size(), MSG_NOSIGNAL) == -1) {
            cout << "Error sending command: " << strerror(errno) << endl;
            continue;
        }
        cout << "Sent to tracker: " << inptline << endl;

    char buffer[1024] = {0};
     int bytesRead = read(sock, buffer, sizeof(buffer) - 1);
     if (bytesRead > 0) {
        buffer[bytesRead] = '\0'; // Null-terminate the string
        cout << "Tracker response: " << buffer << endl;
        string response(buffer);
        if(inpt[0] == "login"){
        if(response == "Login Successful"){
            loggedIn = true;
           
        }
    }
    } else if (bytesRead < 0) {
        cout << "Error reading from tracker: " << strerror(errno) << endl;
    } else {
        cout << "Tracker closed the connection." << endl;
    }
    
    }
    close(sock);
    return 0; 
}
