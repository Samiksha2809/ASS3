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

string log_file_Name = "logfile.txt";
mutex logMutex;

void write_Log(const string &message) {
    lock_guard<mutex> lock(logMutex);  // Lock the mutex to ensure safe access
    ofstream fout(log_file_Name, ios::app);
    if (!fout.is_open()) {
        cerr << "Error: Unable to open log file: " << log_file_Name << endl;
        return;
    }
    fout << message << endl;
    fout.close();
}

vector<string> loadTrackerInfo(const string &file_Name) {
    vector<string> res;
    ifstream file(file_Name);
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

struct client_details {
    int port;
    int client_socket;
    string password;
    string username;
};

struct group_details {
    string admin;
    map<string, client_details> members;
    map<string, vector<client_details>> filetoports;
};

string tracker1Ip, tracker2Ip, currentTrackerIP, seederfile_Name;
int tracker1Port, tracker2Port, curTrackerPort;
unordered_map<string, string> users;
unordered_map<string, bool> isLoggedIn;
unordered_map<string, unordered_map<string, set<string>>> grpfileseeder; // grpfilesseder = groupid -> {map of file_Names -> peer address}
unordered_map<string, string> fileSize;
unordered_map<string, string> group_admins;
vector<string> allGroups;
unordered_map<string, set<string>> group_Members;
unordered_map<string, set<string>> group_pendingRequests;
unordered_map<string, string> unameToPort;
unordered_map<string, string> hash_Piecewise;


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
    struct stat store;
    return (stat(path.c_str(), &store) == 0); // Check if the file exists
}


void uploadFile( int client_socket, string client_userid,vector<string> inpt) {
    // inpt - upload_file <file_path> <group_id>
    if (inpt.size() != 3) {
        string response = "Argumnet count invalid";
        write(client_socket, response.c_str(), response.size());
        return; 
    }
    
    string groupId = inpt[2];
    if (group_Members.find(groupId) == group_Members.end()) {
        string response = "Sorry, Group doesn't exist";
        write(client_socket, response.c_str(), response.size());
        return;
    }
    
    if (group_Members[groupId].find(client_userid) == group_Members[groupId].end()) {
        string response = "User not a member of the group";
        write(client_socket, response.c_str(), response.size());
        return;
    }
    
    
    if (!pathExists(inpt[1])) {
        string response = "File path doesn't exist";
        write(client_socket, response.c_str(), response.size());
        return;
    }
    
    char fileDetails[524288] = {0}; 
    write(client_socket, "Uploading file", 15);
    cout<<"uploading"<< endl;
    write_Log("uploading");

  
    if (read(client_socket, fileDetails, sizeof(fileDetails)) > 0) {
        //cout << "reading" << endl;
        if (string(fileDetails) == "error") return; 
        write_Log(fileDetails);
        vector<string> filedet = splitString(string(fileDetails), "$$");
        // filedet = [filepath, peer address, file size, file hash, piecewise hash] 
        string file_Name = splitString(string(filedet[0]), "/").back(); // Extract the file_Name
        write_Log(file_Name);
        // Construct piecewise hash from the received details
        string hashOfPieces = "";
        for (int i = 4; i < filedet.size(); i++) {
            hashOfPieces += filedet[i];
            if (i != filedet.size() - 1) hashOfPieces += "$$";
        }
        //cout<<"hello";
        
        // Store the piecewise hash and update the seeder list
        hash_Piecewise[file_Name] = hashOfPieces;
        grpfileseeder[groupId][file_Name].insert(client_userid);

        // Update the file size map
        fileSize[file_Name] = filedet[2]; // Assuming filedet[2] contains the file size

        // cout<<"hello";
        write(client_socket, "Uploaded", 8); // Notify client of success
        cout << "uploaded"<<endl;
        write_Log("uploaded");
    } else {
        string response = "Failed to read the file data";
        write(client_socket, response.c_str(), response.size());
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

int createGroup(vector<string> inpt, int client_socket, string client_userid) {
    if (inpt.size() != 2) {
        string response = "Argument count is invalid";
        write(client_socket, response.c_str(), response.size());
        return -1;
    }
    for (auto i : allGroups) {
        if (i == inpt[1]) return -1;
    }
    group_admins.insert({inpt[1], client_userid});
    allGroups.push_back(inpt[1]);
    group_Members[inpt[1]].insert(client_userid);
    return 0;
}

void list_groups( int client_socket,vector<string> inpt) {
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

void join_group(int client_socket, string client_userid,vector<string> inpt) {
    if (inpt.size() != 2) {
        string response = "Invalid argument count";
        write(client_socket, response.c_str(), response.size());
        return;
    }

    if (group_admins.find(inpt[1]) == group_admins.end()) {
        string response = "Invalid Group";
        write(client_socket, response.c_str(), response.size());
    } else if (group_Members[inpt[1]].find(client_userid) == group_Members[inpt[1]].end()) {
        group_pendingRequests[inpt[1]].insert(client_userid);
        string response = "Sent Group Request";
        write(client_socket, response.c_str(), response.size());
    } else {
        string response = "You are already present in this group";
        write(client_socket, response.c_str(), response.size());
    }
}

void list_requests( int client_socket, string client_userid,vector<string> inpt) {
    if (inpt.size() != 2) {
        string response = "Argument count invalid";
        write(client_socket, response.c_str(), response.size());
        return;
    }

    if (group_admins.find(inpt[1]) == group_admins.end() || group_admins[inpt[1]] != client_userid) {
        string response = "Group invalid or not admin";
        write(client_socket, response.c_str(), response.size());
    } else if (group_pendingRequests[inpt[1]].empty()) {
        string response = "No pending requests";
        write(client_socket, response.c_str(), response.size());
    } else {
        string reply = "Pending requests:\n";
        for (const auto& request : group_pendingRequests[inpt[1]]) {
            reply += request + "\n";
        }
        write(client_socket, reply.c_str(), reply.size());
    }
}

void accept_request(int client_socket, string client_userid,vector<string> inpt) {
    if (inpt.size() != 3) {
        write(client_socket, "Invalid argument count", 22);
        return;
    }

    if (group_admins.find(inpt[1]) == group_admins.end()) {
        string response = "Invalid group id";
        write(client_socket, response.c_str(), response.size());
    } else if (group_admins[inpt[1]] == client_userid) {
        group_pendingRequests[inpt[1]].erase(inpt[2]);
        group_Members[inpt[1]].insert(inpt[2]);
        write(client_socket, "Request accepted.", 18);
    } else {
        write(client_socket, "You are not the admin of this group", 35);
    }
}

void leave_group( int client_socket, string client_userid,vector<string> inpt) {
    if (inpt.size() != 2) {
        string response = "Argument count invalid";
        write(client_socket, response.c_str(), response.size());
        return;
    }

    string group_id = inpt[1];

    if (group_admins.find(group_id) == group_admins.end()) {
        string response = "Group does not exist";
        write(client_socket, response.c_str(), response.size());
        return;
    }

    if (group_Members[group_id].find(client_userid) != group_Members[group_id].end()) {
        if (group_admins[group_id] == client_userid) {
            if (group_Members[group_id].size() > 1) {
                string new_admin_uid = *group_Members[group_id].begin();
                group_admins[group_id] = new_admin_uid;
                write(client_socket, "You left the group. A new admin has been assigned.", 51);
            } else {
                group_admins.erase(group_id);
                group_Members.erase(group_id);
                group_pendingRequests.erase(group_id);
                auto it = find(allGroups.begin(), allGroups.end(), group_id);
                if (it != allGroups.end()) {
                    allGroups.erase(it);
                }
                write(client_socket, "You were the last member. Group deleted successfully.", 54);
            }
        } else {
            group_Members[group_id].erase(client_userid);
            write(client_socket, "Group left successfully", 23);
        }
    } else {
        write(client_socket, "You are not in this group", 25);
    }
}

void handle_client(int client_socket) {
    string client_userid = "";
    cout << ("pthread started for client socket number " + to_string(client_socket))<<endl;
    write_Log("pthread started for client socket number " + to_string(client_socket));

    while (true) {
        char inptline[1024] = {0};

        if (read(client_socket, inptline, 1024) <= 0) {
            isLoggedIn[client_userid] = false;
            close(client_socket);
            break;
        }
        cout << ("client request: " + string(inptline)) << endl;
        write_Log("client request: " + string(inptline));

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
                string response = "One login session is already active";
                write(client_socket, response.c_str(), response.size());
            } else if (r < 0) {
                write(client_socket, "Incorrect Username/password", 28);
            } else {
                string response = "Login Successful";
                write(client_socket, response.c_str(), response.size());
                client_userid = inpt[1];
            }
        } else if (inpt[0] == "create_group") {
            int r = createGroup(inpt, client_socket, client_userid);
            if (r >= 0) {
                string response = "Group Created";
                write(client_socket, response.c_str(), response.size());
            } else {
                string response = "Group exists";
                write(client_socket, response.c_str(), response.size());
            }
        } else if (inpt[0] == "list_groups") {
            list_groups(client_socket,inpt);
        } else if (inpt[0] == "join_group") {
            join_group(client_socket, client_userid,inpt);
        } else if (inpt[0] == "list_requests") {
            list_requests( client_socket, client_userid,inpt);
        } else if (inpt[0] == "accept_request") {
            accept_request(client_socket, client_userid,inpt);
        } else if (inpt[0] == "leave_group") {
            leave_group(client_socket, client_userid,inpt);
        } else if (inpt[0] == "upload_file") {
          uploadFile(client_socket, client_userid,inpt);
        }
    }
}

int main(int argc, char *argv[]) {
    if (argc != 3) {
        cout << "Provide arguments as <tracker info file name> and <tracker_number>\n"<<endl;
        write_Log("Provide arguments as <tracker info file name> and <tracker_number>\n");
        return -1;
    }

    string tracker_info = argv[1];
    int tracker_no = stoi(argv[2]);
    vector<string> trackerAddress = loadTrackerInfo(tracker_info);

    if (tracker_no == 1) {
        tracker1Ip = trackerAddress[0];
        tracker1Port = stoi(trackerAddress[1]);
        currentTrackerIP = tracker1Ip;
        curTrackerPort = tracker1Port;
    } else {
        tracker2Ip = trackerAddress[2];
        tracker2Port = stoi(trackerAddress[3]);
        currentTrackerIP = tracker2Ip;
        curTrackerPort = tracker2Port;
    }

    int server_fd, client_socket;
    vector<thread> vector_ofThreads;
    struct sockaddr_in address;
    pthread_t exitDetectionThreadId;
    int addressLength = sizeof(address);
    
    server_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (server_fd == 0) {
        cerr << "Socket failed" << endl;
        exit(EXIT_FAILURE);
    }
    cout << "Tracker socket created" << endl;
    write_Log("Tracker Socket created"); 
    
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = INADDR_ANY;
    address.sin_port = htons(curTrackerPort);

    if (bind(server_fd, (struct sockaddr *)&address, sizeof(address)) < 0) {
        cerr << "Bind failed" << endl;
        exit(EXIT_FAILURE);
    }
    cout << "Binding Completed" << endl;
    write_Log("Binding Completed");

    if (listen(server_fd, 3) < 0) {
        cerr << "Listen failed" << endl;
        exit(EXIT_FAILURE);
    }
    
    if (pthread_create(&exitDetectionThreadId, NULL, check_input, NULL) == -1) {
        perror("pthread");
        exit(EXIT_FAILURE);
    }

    while (true) {
        if ((client_socket = accept(server_fd, (struct sockaddr *)&address, (socklen_t *)&addressLength)) < 0) {
            perror("Accepting error");
            cout << "Error in accepting" << endl;
        }
        cout << "Connection Accepted" << endl;
        write_Log("Connection Accepted");

        vector_ofThreads.push_back(thread(handle_client, client_socket));
    }

    for (auto& thread : vector_ofThreads) {
        if (thread.joinable()) {
            thread.join();
        }
    }

    return 0;
}
