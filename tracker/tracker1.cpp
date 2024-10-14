#include <bits/stdc++.h>
#include <sys/stat.h>
#include <openssl/sha.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <signal.h>
#include <string.h>
#include <netdb.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <sys/ioctl.h>
#include <stdarg.h>
#include <fcntl.h>
#include <errno.h>
#include <sys/time.h>
#include <pthread.h>

using namespace std;

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

void* check_input(void* arg) {
    while (true) {
        string ipline;
        getline(cin, ipline);
        if (ipline == "quit") {
            exit(0);
        }
    }
}

struct clientdet {
    int port;
    int clientSock;
    string password;
    string username;
};

struct group_details {
    string admin;
    map<string, clientdet> members;
    map<string, vector<clientdet>> filetoports;
};

string logFileName, tracker1_ip, tracker2_ip, curTrackerIP, seederFileName;
int tracker1Port, tracker2Port, curTrackerPort;
unordered_map<string, string> users;
unordered_map<string, bool> isLoggedIn;
unordered_map<string, unordered_map<string, set<string>>> seederList; // groupid -> {map of filenames -> peer address}
unordered_map<string, string> fileSize;
unordered_map<string, string> grpAdmins;
vector<string> allGroups;
unordered_map<string, set<string>> groupMembers;
unordered_map<string, set<string>> grpPendngRequests;
unordered_map<string, string> unameToPort;
unordered_map<string, string> piecewiseHash;


vector<string> splitString(const string &s, const string &delimiter) {
    vector<string> tokens;
    size_t last = 0, next = 0;
    while ((next = s.find(delimiter, last)) != string::npos) {
        tokens.push_back(s.substr(last, next - last));
        last = next + delimiter.length();
    }
    tokens.push_back(s.substr(last)); // Add the last token
    return tokens;
}

bool pathExists(const string& path) {
    struct stat buffer;
    return (stat(path.c_str(), &buffer) == 0); // Check if the file exists
}


void uploadFile(vector<string> inpt, int client_socket, string client_uid) {
    // inpt - upload_file <file_path> <group_id>
    if (inpt.size() != 3) {
        write(client_socket, "Invalid argument count", 22);
        return; // Ensure to exit after sending a response
    }
    
    string groupId = inpt[2];
    if (groupMembers.find(groupId) == groupMembers.end()) {
        write(client_socket, "Error 101: Group does not exist", 32);
        return;
    }
    
    if (groupMembers[groupId].find(client_uid) == groupMembers[groupId].end()) {
        write(client_socket, "Error 102: User not a member of the group", 41);
        return;
    }
    
    // Check if the file path exists
    if (!pathExists(inpt[1])) {
        write(client_socket, "Error 103: File path does not exist", 36);
        return;
    }
    
    char fileDetails[524288] = {0}; // Buffer to receive file details
    write(client_socket, "Uploading...", 12);
    cout<<("uploading")<< endl;

    // Read the file data from the client
    if (read(client_socket, fileDetails, sizeof(fileDetails)) > 0) {
        if (string(fileDetails) == "error") return; // Handle error response

        vector<string> fdet = splitString(string(fileDetails), "$$");
        // fdet = [filepath, peer address, file size, file hash, piecewise hash] 
        string filename = splitString(string(fdet[0]), "/").back(); // Extract the filename
        cout << filename;
        // Construct piecewise hash from the received details
        string hashOfPieces = "";
        for (size_t i = 4; i < fdet.size(); i++) {
            hashOfPieces += fdet[i];
            if (i != fdet.size() - 1) hashOfPieces += "$$";
        }
        
        // Store the piecewise hash and update the seeder list
        piecewiseHash[filename] = hashOfPieces;
        seederList[groupId][filename].insert(client_uid);

        // Update the file size map
        fileSize[filename] = fdet[2]; // Assuming fdet[2] contains the file size

        write(client_socket, "Uploaded", 8); // Notify client of success
        cout << "uploaded";
    } else {
        write(client_socket, "Error: Failed to read file data", 33);
    }
}


int createUser(vector<string> input, int client_socket) {
    if (input.size() != 3) {
        string response = "Invalid input.";
        write(client_socket, response.c_str(), response.size());
        return -1;
    }
    string username = input[1];
    string password = input[2];

    if (users.find(username) == users.end()) {
        users.insert({username, password});
        string response = "User created successfully.";
        write(client_socket, response.c_str(), response.size());
    } else {
        string response = "User already exists";
        write(client_socket, response.c_str(), response.size());
    }
    return 0;
}

int LoginUser(vector<string> input, int client_socket) {
    if (input.size() != 3) {
        string response = "Invalid input.";
        write(client_socket, response.c_str(), response.size());
        return -1;
    }
    string username = input[1];
    string password = input[2];

    if (users.find(username) == users.end() || users[username] != password) {
        string response = "Invalid user_id or password!";
        write(client_socket, response.c_str(), response.size());
        return -1;
    }

    if (isLoggedIn.find(username) == isLoggedIn.end()) {
        isLoggedIn.insert({username, true});
    } else {
        if (isLoggedIn[username]) {
            string response = "User already Logged in";
            write(client_socket, response.c_str(), response.size());
            return -1;
        }
        isLoggedIn[username] = true;
    }
    return 0;
}

int createGroup(vector<string> inpt, int client_socket, string client_uid) {
    if (inpt.size() != 2) {
        string response = "Argument count is invalid";
        write(client_socket, response.c_str(), response.size());
        return -1;
    }
    for (auto i : allGroups) {
        if (i == inpt[1]) return -1;
    }
    grpAdmins.insert({inpt[1], client_uid});
    allGroups.push_back(inpt[1]);
    groupMembers[inpt[1]].insert(client_uid);
    return 0;
}

void list_groups(vector<string> inpt, int client_socket) {
    if (inpt.size() != 1) {
        string response = "Argument count invalid";
        write(client_socket, response.c_str(), response.size());
        return;
    }

    if (allGroups.empty()) {
        write(client_socket, "No groups found", 16);
        return;
    }

    string reply = "Groups:\n";
    for (const auto& group : allGroups) {
        reply += group + "\n";
    }
    write(client_socket, reply.c_str(), reply.size());
}

void join_group(vector<string> inpt, int client_socket, string client_uid) {
    if (inpt.size() != 2) {
        string response = "Invalid argument count";
        write(client_socket, response.c_str(), response.size());
        return;
    }

    if (grpAdmins.find(inpt[1]) == grpAdmins.end()) {
        string response = "Invalid Group";
        write(client_socket, response.c_str(), response.size());
    } else if (groupMembers[inpt[1]].find(client_uid) == groupMembers[inpt[1]].end()) {
        grpPendngRequests[inpt[1]].insert(client_uid);
        string response = "Sent Group Request";
        write(client_socket, response.c_str(), response.size());
    } else {
        string response = "You are already in this group";
        write(client_socket, response.c_str(), response.size());
    }
}

void list_requests(vector<string> inpt, int client_socket, string client_uid) {
    if (inpt.size() != 2) {
        string response = "Argument count invalid";
        write(client_socket, response.c_str(), response.size());
        return;
    }

    if (grpAdmins.find(inpt[1]) == grpAdmins.end() || grpAdmins[inpt[1]] != client_uid) {
        string response = "Group invalid or not admin";
        write(client_socket, response.c_str(), response.size());
    } else if (grpPendngRequests[inpt[1]].empty()) {
        string response = "No pending requests";
        write(client_socket, response.c_str(), response.size());
    } else {
        string reply = "Pending requests:\n";
        for (const auto& request : grpPendngRequests[inpt[1]]) {
            reply += request + "\n";
        }
        write(client_socket, reply.c_str(), reply.size());
    }
}

void accept_request(vector<string> inpt, int client_socket, string client_uid) {
    if (inpt.size() != 3) {
        write(client_socket, "Invalid argument count", 22);
        return;
    }

    if (grpAdmins.find(inpt[1]) == grpAdmins.end()) {
        string response = "Invalid group id";
        write(client_socket, response.c_str(), response.size());
    } else if (grpAdmins[inpt[1]] == client_uid) {
        grpPendngRequests[inpt[1]].erase(inpt[2]);
        groupMembers[inpt[1]].insert(inpt[2]);
        write(client_socket, "Request accepted.", 18);
    } else {
        write(client_socket, "You are not the admin of this group", 35);
    }
}

void leave_group(vector<string> inpt, int client_socket, string client_uid) {
    if (inpt.size() != 2) {
        string response = "Argument count invalid";
        write(client_socket, response.c_str(), response.size());
        return;
    }

    string group_id = inpt[1];

    if (grpAdmins.find(group_id) == grpAdmins.end()) {
        string response = "Group does not exist";
        write(client_socket, response.c_str(), response.size());
        return;
    }

    if (groupMembers[group_id].find(client_uid) != groupMembers[group_id].end()) {
        if (grpAdmins[group_id] == client_uid) {
            if (groupMembers[group_id].size() > 1) {
                string new_admin_uid = *groupMembers[group_id].begin();
                grpAdmins[group_id] = new_admin_uid;
                write(client_socket, "You left the group. A new admin has been assigned.", 51);
            } else {
                grpAdmins.erase(group_id);
                groupMembers.erase(group_id);
                grpPendngRequests.erase(group_id);
                auto it = find(allGroups.begin(), allGroups.end(), group_id);
                if (it != allGroups.end()) {
                    allGroups.erase(it);
                }
                write(client_socket, "You were the last member. Group deleted successfully.", 54);
            }
        } else {
            groupMembers[group_id].erase(client_uid);
            write(client_socket, "Group left successfully", 23);
        }
    } else {
        write(client_socket, "You are not in this group", 25);
    }
}

void handle_client(int client_socket) {
    string client_uid = "";
    cout << ("pthread started for client socket number " + to_string(client_socket));

    while (true) {
        char inptline[1024] = {0};

        if (read(client_socket, inptline, 1024) <= 0) {
            isLoggedIn[client_uid] = false;
            close(client_socket);
            break;
        }
        cout << ("client request: " + string(inptline)) << endl;

        string s, in = string(inptline);
        stringstream ss(in);
        vector<string> inpt;

        while (ss >> s) {
            inpt.push_back(s);
        }

        if (inpt[0] == "create_user") {
            createUser(inpt, client_socket);
        } else if (inpt[0] == "login") {
            int r = LoginUser(inpt, client_socket);
            if (r > 0) {
                write(client_socket, "You already have one active session", 35);
            } else if (r < 0) {
                write(client_socket, "Incorrect Username/password", 28);
            } else {
                string response = "Login Successful";
                write(client_socket, response.c_str(), response.size());
                client_uid = inpt[1];
            }
        } else if (inpt[0] == "create_group") {
            int r = createGroup(inpt, client_socket, client_uid);
            if (r >= 0) {
                string response = "Group Created";
                write(client_socket, response.c_str(), response.size());
            } else {
                string response = "Group exists";
                write(client_socket, response.c_str(), response.size());
            }
        } else if (inpt[0] == "list_groups") {
            list_groups(inpt, client_socket);
        } else if (inpt[0] == "join_group") {
            join_group(inpt, client_socket, client_uid);
        } else if (inpt[0] == "list_requests") {
            list_requests(inpt, client_socket, client_uid);
        } else if (inpt[0] == "accept_request") {
            accept_request(inpt, client_socket, client_uid);
        } else if (inpt[0] == "leave_group") {
            leave_group(inpt, client_socket, client_uid);
        } else if (inpt[0] == "upload_file") {
          uploadFile(inpt, client_socket, client_uid);
        }
    }
}

int main(int argc, char *argv[]) {
    if (argc != 3) {
        cout << "Provide arguments as <tracker info file name> and <tracker_number>\n";
        return -1;
    }

    string tracker_info = argv[1];
    int tracker_no = stoi(argv[2]);
    vector<string> trackerAddress = loadTrackerInfo(tracker_info);

    if (tracker_no == 1) {
        tracker1_ip = trackerAddress[0];
        tracker1Port = stoi(trackerAddress[1]);
        curTrackerIP = tracker1_ip;
        curTrackerPort = tracker1Port;
    } else {
        tracker2_ip = trackerAddress[2];
        tracker2Port = stoi(trackerAddress[3]);
        curTrackerIP = tracker2_ip;
        curTrackerPort = tracker2Port;
    }

    int server_fd, client_socket;
    vector<thread> threadVector;
    struct sockaddr_in address;
    pthread_t exitDetectionThreadId;
    int addrlen = sizeof(address);
    
    server_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (server_fd == 0) {
        cerr << "Socket failed" << endl;
        exit(EXIT_FAILURE);
    }
    cout << "Tracker socket created" << endl;
    
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = INADDR_ANY;
    address.sin_port = htons(curTrackerPort);

    if (bind(server_fd, (struct sockaddr *)&address, sizeof(address)) < 0) {
        cerr << "Bind failed" << endl;
        exit(EXIT_FAILURE);
    }
    cout << "Binding Completed" << endl;

    if (listen(server_fd, 3) < 0) {
        cerr << "Listen failed" << endl;
        exit(EXIT_FAILURE);
    }
    
    if (pthread_create(&exitDetectionThreadId, NULL, check_input, NULL) == -1) {
        perror("pthread");
        exit(EXIT_FAILURE);
    }

    while (true) {
        if ((client_socket = accept(server_fd, (struct sockaddr *)&address, (socklen_t *)&addrlen)) < 0) {
            perror("Acceptance error");
            cout << "Error in accept" << endl;
        }
        cout << "Connection Accepted" << endl;

        threadVector.push_back(thread(handle_client, client_socket));
    }

    for (auto& thread : threadVector) {
        if (thread.joinable()) {
            thread.join();
        }
    }

    return 0;
}
