#include <bits/stdc++.h>
#include <arpa/inet.h> 
#include <sys/socket.h> 
#include <unistd.h>
#include <pthread.h>
#include <fcntl.h>
using namespace std;

string tracker1_ip, tracker2_ip;
int tracker1Port, tracker2Port, peer_port;
bool loggedIn;

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
        curTrackIP = &tracker1_ip[0]; 
        curTrackPort = tracker1Port;
    } else {
        curTrackIP = &tracker2_ip[0]; 
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
    string peer_info = argv[1];
    string tracker_info = argv[2];
    vector<string> trackerAddress = loadTrackerInfo(tracker_info);

    // Initialize tracker addresses
    tracker1_ip = trackerAddress[0];
    tracker1Port = stoi(trackerAddress[1]);
    tracker2_ip = trackerAddress[2];
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
            cout << "You already have one active session\n";
            continue;
        }
        if (inpt[0] != "login" && inpt[0] != "create_user" && !loggedIn) {
            cout << "Please login / create an account\n";
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
            // string peerAddress = peer_ip + ":" + to_string(peer_port);
            // write(sock, &peerAddress[0], peerAddress.length());
           
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
