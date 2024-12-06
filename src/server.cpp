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
#include <fstream>
#include <unordered_map>
#include "zlib.h"
using namespace std;

struct HttpRequest {
  unordered_map<string, string> headers;
  string body;
  string method;
  string path;
  string version;
};

struct HttpResponse {
  unordered_map<string, string> headers;
  string body;
  string status;
  string status_code;
  string version;
};

struct GzipCompressed {
  string compressed_data;
  unsigned long bytes;
};

GzipCompressed getGzipCompressed(string content) {
  z_stream zs;
  memset(&zs, 0, sizeof(zs));

  // initialize zlib
  int initStatus = deflateInit2(&zs, Z_BEST_COMPRESSION, Z_DEFLATED, 15 + 16, 8, Z_DEFAULT_STRATEGY);
  if(initStatus != Z_OK) {
    cout<<"initStatus = "<<initStatus<<endl;
    throw std::runtime_error("deflateInit2 failed");
    return GzipCompressed{};
  }

  //deflate the data
  zs.next_in = (Bytef *)content.c_str();
  zs.avail_in = content.length();
  int ret = Z_OK;
  char outbuffer[32768];  // 4 Bytes of data
  string outstring;
  while(ret == Z_OK) {
    zs.next_out = reinterpret_cast<Bytef *>(outbuffer);
    zs.avail_out = sizeof(outbuffer);

    ret = deflate(&zs, Z_FINISH);

    if(outstring.length() < zs.total_out) {
      outstring.append(outbuffer, zs.total_out - outstring.length()); // get the data from the buffer, length using total_out - length of outstring
    }
  }

  //clean up
  deflateEnd(&zs);
  if(ret != Z_STREAM_END) {
    throw std::runtime_error("deflate failed");
  }
  
  GzipCompressed gzipCompressed = {
    compressed_data: outstring,
    bytes: zs.total_out
  };
  return gzipCompressed;
}

HttpRequest parseRequest(string request) {
  HttpRequest httpRequest;
  vector<string> request_parts;
  size_t pos = 0;
  size_t pos1 = 0;
  string token;
  string delimiter = "\r\n";

  // fetching the request line
  string request_line = request.substr(0, request.find("\r\n"));
  request.erase(0, request.find("\r\n") + 2);
  httpRequest.method = request_line.substr(0, request_line.find(" "));
  request_line.erase(0, request_line.find(" ") + 1);
  httpRequest.path = request_line.substr(0, request_line.find(" "));
  request_line.erase(0, request_line.find(" ") + 1);
  httpRequest.version = request_line;

  while((pos = request.find(delimiter)) != string::npos) {
    cout<<"before token = "<<token<<endl;
    token = request.substr(0, pos);
    string key = token.substr(0, token.find(":"));
    token.erase(0, token.find(":") + 2);
    
    if(key == "Accept-Encoding") {
      string value = "";
      cout<<"token = "<<token<<endl;
      while((pos1 = token.find(", ")) != string::npos) {
        string encoding = token.substr(0, pos1);
        cout<<"encoding = "<<encoding<<"pos1 = "<<pos1<<endl;
        if(encoding == "gzip") {
          value += value == "" ? encoding : ", " + encoding;
        }
        token.erase(0, pos1 + 2);
      }
      if(token == "gzip") {
        value += value == "" ? token : ", " + token;
      }
      httpRequest.headers[key] = value;
    } else {
      string value = token;
      httpRequest.headers[key] = value;
    }
    request.erase(0, pos + delimiter.length());
  }

  // fetching the body
  httpRequest.body = request;
  return httpRequest;
}

string getHttpResponse(HttpResponse httpResponse) {
  string response = "HTTP/" + httpResponse.version + " " + httpResponse.status + " " + httpResponse.status_code + "\r\n";
  for(auto it = httpResponse.headers.begin(); it != httpResponse.headers.end(); it++) {
    response += it->first + ": " + it->second + "\r\n";
  }
  response += "\r\n" + httpResponse.body;
  return response;
}

int handleRequests_2(int client) {
  string basepath = "/tmp/data/codecrafters.io/http-server-tester/";
  string cli_message(1024, '\0');
  size_t recvdbytes = recv(client, cli_message.data(), cli_message.size(), 0);
  if(recvdbytes < 0) {
    std::cerr << "Failed to receive message from client\n";
    return 1;
  }
  HttpRequest httpRequest = parseRequest(cli_message);
  HttpResponse httpResponse;
  httpResponse.version = "1.1";
  httpResponse.status = "200";
  httpResponse.status_code = "OK";
  cout<<"httpRequest.path = "<<httpRequest.path<<endl;
  if(httpRequest.method == "GET") {
    if(httpRequest.path == "/") {
    } else if (httpRequest.path.substr(0, 6) == "/echo/") {
      string echo_string = httpRequest.path.substr(6, httpRequest.path.length() - 6);
      httpResponse.headers["Content-Type"] = "text/plain";
      if(httpRequest.headers.find("Accept-Encoding") != httpRequest.headers.end() && httpRequest.headers["Accept-Encoding"] != "invalid-encoding") {
        httpResponse.headers["Content-Encoding"] = httpRequest.headers["Accept-Encoding"];
        GzipCompressed gzipCompressed = getGzipCompressed(echo_string);
        httpResponse.body = gzipCompressed.compressed_data;
        httpResponse.headers["Content-Length"] = to_string(gzipCompressed.bytes);
      } else {
        httpResponse.body = echo_string;
        httpResponse.headers["Content-Length"] = to_string(echo_string.length());
      }
      
    } else if (httpRequest.path.substr(0, 11) == "/user-agent") {
      string user_agent = httpRequest.headers["User-Agent"];
      httpResponse.headers["Content-Type"] = "text/plain";
      httpResponse.headers["Content-Length"] = to_string(user_agent.length());
      httpResponse.body = user_agent;
    } else if (httpRequest.path.substr(0, 7) == "/files/") {
      string filename = httpRequest.path.substr(7, httpRequest.path.length() - 7);
      std::ifstream infile(basepath + filename);
      if (infile.good()) {
        cout<<"File is good"<<endl;
        size_t chars_read;
        char buffer[10000];
        // Read file
        if (!(infile.read(buffer, sizeof(buffer)))) { // Read up to the size of the buffer
            if (!infile.eof()) { // End of file is an expected condition here and not worth 
                                // clearing. What else are you going to read?
                                // Something went wrong while reading. Find out what and handle.
            }
        }

        chars_read = infile.gcount(); // Get amount of characters really read.
        cout<<"chars_read = "<<chars_read<<endl;
        httpResponse.headers["Content-Type"] = "application/octet-stream";
        httpResponse.headers["Content-Length"] = to_string(chars_read);
        httpResponse.body = string(buffer, chars_read);
      } else {
        httpResponse.status = "404";
        httpResponse.status_code = "Not Found";
        cout<<"File is not good"<<endl;
      }
    } else {
      httpResponse.status = "404";
      httpResponse.status_code = "Not Found";
    }
  } else if (httpRequest.method == "POST") {
    if(httpRequest.path.substr(0, 7) == "/files/") {
      string filename = httpRequest.path.substr(7, httpRequest.path.length() - 7);
      ofstream outfile(basepath + filename);
      int content_length = stoi(httpRequest.headers["Content-Length"]);
      outfile << httpRequest.body.substr(0, content_length);
      outfile.close();
      httpResponse.status = "201";
      httpResponse.status_code = "Created";
    }
  }
  string response = getHttpResponse(httpResponse);
  size_t sentbytes = send(client, response.c_str(), response.length(), 0);
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
    std::thread th(handleRequests_2, client);
    th.detach();
  }

  close(server_fd);

  return 0;
}
