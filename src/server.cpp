#include <iostream>
#include <cstdlib>
#include <string>
#include <cstring>
#include <unistd.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <regex>
#include <thread>
using namespace std;

int handleRequests(int client) {
  cout<<"handling client = "<<client<<endl;
  string cli_message(1024, '\0');
  size_t recvdbytes = recv(client, cli_message.data(), cli_message.size(), 0);
  string request = cli_message.substr(0, cli_message.find("\r\n"));
  vector<string> request_parts;
  size_t pos = 0;
  string token;
  string delimiter = "\r\n";
  while ((pos = cli_message.find(delimiter)) != string::npos) {
    token = cli_message.substr(0, pos);
    request_parts.push_back(token);
    cli_message.erase(0, pos + delimiter.length());
  }
  request_parts.push_back(cli_message);
  if(recvdbytes < 0) {
    std::cerr << "Failed to receive message from client\n";
    return 1;
  }
  cout<<"cli_message = "<<request<<endl;
  string message = "";
  string pattern = R"(^GET \/echo\/[^\s]+ HTTP\/1\.1$)";
  string pattern3 = R"(^GET \/user-agent HTTP\/1\.1$)";
  string pattern2 = R"(^GET \/ HTTP\/1\.1$)";
  if(request.substr(0, 3) == "GET") {
    if(regex_match(request, regex(pattern2))) {
      cout<<"replying from client = "<<client<<endl;
      message = "HTTP/1.1 200 OK\r\n\r\n";
    } else if (regex_match(request, regex(pattern3))) {
      cout<<"came here.....";
      string user_agent = request_parts[2].replace(0, 12, "");
      // cout<<"requests_parts.size() = "<<request_parts.size()<<" request_parts[0] = "<<request_parts[0]<<"request_parts[1] = "<<request_parts[1]<<endl;
      message = "HTTP/1.1 200 OK\r\nContent-Type: text/plain\r\nContent-Length:" + to_string(user_agent.length()) + "\r\n\r\n" + user_agent;
    } else if (regex_match(request, regex(pattern))) {
      string var = request.substr(10, request.find("HTTP") - 11);
      int len = var.length();
      message = "HTTP/1.1 200 OK\r\nContent-Type: text/plain\r\nContent-Length: " + to_string(len) + "\r\n\r\n" + var;
    } else {
      message = "HTTP/1.1 404 Not Found\r\n\r\n";
    }
  }
  // string message = cli_message.substr(0, 16) == "GET / HTTP/1.1\r\n" ? "HTTP/1.1 200 OK\r\n\r\n" : "HTTP/1.1 404 Not Found\r\n\r\n";
  size_t sentbytes = send(client, message.c_str(), message.length(), 0);
  if(sentbytes < 0) {
    std::cerr << "Failed to send message to client\n";
    return 1;
  }
  close(client);
  return 0;
}

int main(int argc, char **argv) {
  // Flush after every std::cout / std::cerr
  std::cout << std::unitbuf;
  std::cerr << std::unitbuf;
  
  // You can use print statements as follows for debugging, they'll be visible when running tests.
  std::cout << "Logs from your program will appear here!\n";

  // Uncomment this block to pass the first stage
  //
  int server_fd = socket(AF_INET, SOCK_STREAM, 0);
  if (server_fd < 0) {
   std::cerr << "Failed to create server socket\n";
   return 1;
  }
  //
  // // Since the tester restarts your program quite often, setting SO_REUSEADDR
  // // ensures that we don't run into 'Address already in use' errors
  int reuse = 1;
  if (setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse)) < 0) {
    std::cerr << "setsockopt failed\n";
    return 1;
  }
  //
  struct sockaddr_in server_addr;
  server_addr.sin_family = AF_INET;
  server_addr.sin_addr.s_addr = INADDR_ANY;
  server_addr.sin_port = htons(4221);
  
  if (bind(server_fd, (struct sockaddr *) &server_addr, sizeof(server_addr)) != 0) {
    std::cerr << "Failed to bind to port 4221\n";
    return 1;
  }
  //
  int connection_backlog = 5;
  if (listen(server_fd, connection_backlog) != 0) {
    std::cerr << "listen failed\n";
    return 1;
  }
  
  struct sockaddr_in client_addr;
  int client_addr_len = sizeof(client_addr);

  while(true) {
    std::cout << "Waiting for a client to connect...\n";
    int client = accept(server_fd, (struct sockaddr *) &client_addr, (socklen_t *) &client_addr_len);
    cout<<"client = "<<client<<endl;
    if(client < 0) {
      std::cerr << "Failed to accept client connection\n";
      return 1;
    }
    std::cout << "Client connected\n";
    std::thread th(handleRequests, client);
    th.detach();
  }

  close(server_fd);

  return 0;
}
