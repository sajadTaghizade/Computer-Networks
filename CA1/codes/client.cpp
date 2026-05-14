#include <iostream>
#include <string>
#include <thread>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <cstring>
#include <fstream>
#include <sstream>

#define PORT 8080
#define BUFFER_SIZE 1024

using namespace std;

class ChatClient
{
private:
    int client_socket;
    bool is_running;
    bool is_downloading = false; 
    ofstream outfile; 

    void receive_handler()
    {
        char buffer[BUFFER_SIZE];
        while (is_running)
        {
            memset(buffer, 0, BUFFER_SIZE);
            int bytes_received = recv(client_socket, buffer, BUFFER_SIZE, 0);

            if (bytes_received <= 0)
            {
                cout << "\n disconnected" << endl;
                is_running = false;
                break;
            }

            string data(buffer, bytes_received);

            if (is_downloading) 
            {
                if (data.find("END_FILE\n") != string::npos) 
                {
                    is_downloading = false;
                    outfile.close();
                    cout << "downloaded_file.txt downloaded completely" << endl;
                }
                else
                {
                    outfile.write(buffer, bytes_received); 
                }
            }
            else if (data.find("START_FILE\n") == 0)
            {
                cout << "\n is downloading from server..." << endl;
                outfile.open("downloaded_file.txt", ios::binary);
                is_downloading = true;
                
                int header_len = string("START_FILE\n").length();
                if (bytes_received > header_len) {
                    outfile.write(buffer + header_len, bytes_received - header_len);
                }
            }
            else
            {
                cout << data;
            }
        }
    }

    void send_handler()
    {
        string input;
        while (is_running)
        {
            getline(cin, input); 
            if (input.empty())
                continue;

            send(client_socket, input.c_str(), input.length(), 0);

            if (input == "QUIT")
            {
                is_running = false;
                break;
            }

            istringstream iss(input);
            string command, filename;
            iss >> command >> filename;

            if (command == "PUT" && !filename.empty())
            {
                ifstream file(filename, ios::binary);
                if (!file.is_open())
                {
                    cout << "file " << filename << " not found" << endl;
                    continue;
                }

                cout << "is uploading" << endl;
                char buffer[BUFFER_SIZE];
                while (file.read(buffer, sizeof(buffer)))
                {
                    send(client_socket, buffer, sizeof(buffer), 0);
                    usleep(10000);
                }
                send(client_socket, buffer, file.gcount(), 0); 
                file.close();
                cout << "uploaded" << endl;
            }
        }
    }

public:
    ChatClient()
    {
        is_running = false;
        client_socket = socket(AF_INET, SOCK_STREAM, 0);
        if (client_socket == -1)
        {
            cerr << "Could not create socket" << endl;
            exit(EXIT_FAILURE);
        }
    }

    void connect_to_server(const string &ip)
    {
        sockaddr_in server_address;
        server_address.sin_family = AF_INET;
        server_address.sin_port = htons(PORT);

        if (inet_pton(AF_INET, ip.c_str(), &server_address.sin_addr) <= 0)
        {
            cerr << "Invalid IP address" << endl;
            exit(EXIT_FAILURE);
        }

        if (connect(client_socket, (struct sockaddr *)&server_address, sizeof(server_address)) < 0)
        {
            cerr << "Connection to server failed. Is the server running?" << endl;
            exit(EXIT_FAILURE);
        }

        is_running = true;
        cout << "..........." << endl;
        cout << "Connected to the server successfully" << endl;
        cout << "..........." << endl;

        thread recv_thread(&ChatClient::receive_handler, this);
        thread send_thread(&ChatClient::send_handler, this);    

        send_thread.join();

        recv_thread.detach();
    }

    ~ChatClient()
    {
        if (client_socket != -1)
        {
            close(client_socket);
        }
    }
};

int main()
{
    ChatClient client;
    client.connect_to_server("127.0.0.1");
    return 0;
}
