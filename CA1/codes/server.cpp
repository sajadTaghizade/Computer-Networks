#include <iostream>
#include <string>
#include <thread>
#include <vector>
#include <map>
#include <mutex>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <dirent.h>
#include <fstream>
#include <sstream>
#include <cstring>

#define PORT 8080
#define BUFFER_SIZE 1024

using namespace std;

class ChatServer
{
private:
    int server_socket;
    map<int, string> clients; 
    mutex clients_mutex;      

    void send_to_client(int client_socket, const string &message)
    {
        send(client_socket, message.c_str(), message.length(), 0);
    }

    void broadcast(const string &message, int sender_socket)
    {
        lock_guard<mutex> lock(clients_mutex);
        for (auto const &client : clients)
        {
            if (client.first != sender_socket)
            {
                send(client.first, message.c_str(), message.length(), 0);
            }
        }
    }

    void send_file_list(int client_socket)
    {
        DIR *dir;
        struct dirent *ent;
        string file_list = "\n server file list \n";

        if ((dir = opendir(".")) != NULL)
        {
            while ((ent = readdir(dir)) != NULL)
            {
                if (ent->d_name[0] != '.')
                {
                    file_list += string(ent->d_name) + "\n";
                }
            }
            closedir(dir);
        }
        else
        {
            file_list = "[server directory faild]\n";
        }
        send_to_client(client_socket, file_list + "--------------------------\n");
    }

    void handle_get_file(int client_socket, const string &filename)
    {
        ifstream file(filename, ios::binary);
        if (!file.is_open())
        {
            send_to_client(client_socket, "[file" + filename + " not found]\n");
            return;
        }

        send_to_client(client_socket, "START_FILE\n");
        usleep(20000);

        char buffer[BUFFER_SIZE];
        while (file.read(buffer, sizeof(buffer)))
        {
            send(client_socket, buffer, sizeof(buffer), 0);
            usleep(5000); 
        }
        send(client_socket, buffer, file.gcount(), 0);
        file.close();
        usleep(10000);
        send_to_client(client_socket, "END_FILE\n");
    }

    void handle_put_file(int client_socket, const string &filename)
    {
        ofstream outfile("server_" + filename, ios::binary);
        if (!outfile.is_open())
        {
            send_to_client(client_socket, "[server makefile faild]\n");
            return;
        }

        char buffer[BUFFER_SIZE];
        usleep(50000); 

        int bytes_received;
        while ((bytes_received = recv(client_socket, buffer, BUFFER_SIZE, MSG_DONTWAIT)) > 0)
        {
            outfile.write(buffer, bytes_received);
        }

        outfile.close();
        send_to_client(client_socket, "[server]: file uploaded.\n");
        cout << "[file " << filename << " uploaded ]" << endl;
    }

    void process_command(int client_socket, const string &input)
    {
        istringstream iss(input);
        string command;
        iss >> command;

        if (command == "USERS")
        {
            lock_guard<mutex> lock(clients_mutex);
            string user_list = "\n onlines \n";
            for (auto const &client : clients)
            {
                user_list += client.second + "\n";
            }
            send_to_client(client_socket, user_list + "---------------\n");
        }
        else if (command == "MSG")
        {
            string msg;
            getline(iss, msg);
            string formatted_msg = "[" + clients[client_socket] + "]: " + msg + "\n";
            broadcast(formatted_msg, client_socket);
        }
        else if (command == "PM")
        {
            string target_user, msg;
            iss >> target_user;
            getline(iss, msg);

            bool found = false;
            lock_guard<mutex> lock(clients_mutex);
            for (auto const &client : clients)
            {
                if (client.second == target_user)
                {
                    string formatted_msg = "[PM from " + clients[client_socket] + "]: " + msg + "\n";
                    send_to_client(client.first, formatted_msg);
                    found = true;
                    break;
                }
            }
            if (!found)
            {
                send_to_client(client_socket, "user not found\n");
            }
        }
        else if (command == "LIST")
        {
            send_file_list(client_socket);
        }
        else if (command == "GET")
        {
            string filename;
            iss >> filename;
            if (!filename.empty())
                handle_get_file(client_socket, filename);
        }
        else if (command == "PUT")
        {
            string filename;
            iss >> filename;
            if (!filename.empty())
                handle_put_file(client_socket, filename);
        }
        else if (command != "QUIT")
        {
            send_to_client(client_socket, "invalid\n");
        }
    }

    void client_handler(int client_socket)
    {
        char buffer[BUFFER_SIZE];
        string username;

        {
            lock_guard<mutex> lock(clients_mutex);
            username = "User_" + to_string(client_socket);
            clients[client_socket] = username;
        }

        cout << username << " connected" << endl;
        send_to_client(client_socket, "[server]: your username " + username + " \n");
        broadcast("[server]: " + username + " join the group\n", client_socket);

        while (true)
        {
            memset(buffer, 0, BUFFER_SIZE);
            int bytes_received = recv(client_socket, buffer, BUFFER_SIZE, 0);

            if (bytes_received <= 0)
            {
                break;
            }

            string data(buffer, bytes_received);

            if (data == "QUIT" || data == "QUIT\n")
            {
                break;
            }

            process_command(client_socket, data);
        }

        {
            lock_guard<mutex> lock(clients_mutex);
            clients.erase(client_socket);
        }

        cout << username << " disconnect" << endl;
        broadcast("[server]: " + username + " left the group\n", -1);
        close(client_socket);
    }

public:
    ChatServer()
    {
        server_socket = socket(AF_INET, SOCK_STREAM, 0);
        if (server_socket == -1)
        {
            cerr << "socket faild" << endl;
            exit(EXIT_FAILURE);
        }

        int opt = 1;
        setsockopt(server_socket, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

        sockaddr_in server_address;
        server_address.sin_family = AF_INET;
        server_address.sin_addr.s_addr = INADDR_ANY;
        server_address.sin_port = htons(PORT);

        if (bind(server_socket, (struct sockaddr *)&server_address, sizeof(server_address)) < 0)
        {
            cerr << "bind faild" << endl;
            exit(EXIT_FAILURE);
        }

        if (listen(server_socket, 10) < 0)
        {
            cerr << "listen faild" << endl;
            exit(EXIT_FAILURE);
        }
    }

    void start()
    {
        cout << "........." << endl;
        cout << "server run on port " << PORT << endl;
        cout << "is waiting" << endl;
        cout << ".........." << endl;

        while (true)
        {
            sockaddr_in client_address;
            socklen_t client_addr_len = sizeof(client_address);

            int client_socket = accept(server_socket, (struct sockaddr *)&client_address, &client_addr_len);
            if (client_socket < 0)
            {
                cerr << "accept error" << endl;
                continue;
            }

            thread client_thread(&ChatServer::client_handler, this, client_socket);
            client_thread.detach(); 
        }
    }

    ~ChatServer()
    {
        close(server_socket);
    }
};

int main()
{
    ChatServer server;
    server.start();
    return 0;
}
