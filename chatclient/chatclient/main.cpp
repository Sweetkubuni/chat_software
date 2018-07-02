#undef UNICODE

#include <iostream>
#include <string>
#include <array>
#include <thread>
#include <mutex>
#include <atomic>
#include "threadsafe_queue.h"

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <winsock2.h>
#include <ws2tcpip.h>
#include <stdlib.h>
#include <stdio.h>

// Need to link with Ws2_32.lib
#pragma comment (lib, "Ws2_32.lib")
// #pragma comment (lib, "Mswsock.lib")

#define DEFAULT_BUFLEN 512
#define DEFAULT_PORT "43594"

const std::string DEFAULT_ADDRESS = "127.0.0.1";

lockless::threadsafe_queue<std::string> out_msg;
lockless::threadsafe_queue<std::string> in_msg;

void cls(HANDLE hConsole)
{
	COORD coordScreen = { 0, 0 };    /* here's where we'll home the
									 cursor */
	BOOL bSuccess;
	DWORD cCharsWritten;
	CONSOLE_SCREEN_BUFFER_INFO csbi; /* to get buffer info */
	DWORD dwConSize;                 /* number of character cells in
									 the current buffer */

	/* get the number of character cells in the current buffer */
	bSuccess = GetConsoleScreenBufferInfo(hConsole, &csbi);
	if (!bSuccess) {
		std::cout << "Error: GetConsoleScreenBufferInfo\n";
	}
	dwConSize = csbi.dwSize.X * csbi.dwSize.Y;

	/* fill the entire screen with blanks */

	bSuccess = FillConsoleOutputCharacter(hConsole, (TCHAR) ' ',
		dwConSize, coordScreen, &cCharsWritten);
	if (!bSuccess) { 
		std::cout << "Error: FillConsoleOutputCharacter\n";
	}

	/* get the current text attribute */

	bSuccess = GetConsoleScreenBufferInfo(hConsole, &csbi);
	if (!bSuccess) { 
		std::cout << "Error: ConsoleScreenBufferInfo\n"; 
	}

	/* now set the buffer's attributes accordingly */

	bSuccess = FillConsoleOutputAttribute(hConsole, csbi.wAttributes,
		dwConSize, coordScreen, &cCharsWritten);
	if (!bSuccess) {
		std::cout << "FillConsoleOutputAttribute";
	}

	/* put the cursor at (0, 0) */

	bSuccess = SetConsoleCursorPosition(hConsole, coordScreen);
	if (!bSuccess) {
		std::cout << "SetConsoleCursorPosition";
	}

	return;
}


bool send_message(SOCKET & ConnectSocket, const std::string & msg) {
	// Send an initial buffer
	if (send(ConnectSocket, msg.c_str(), (int)msg.length(), 0) == SOCKET_ERROR) {
		std::cout << "Error: send failed with the following error -> " << WSAGetLastError();
		return false;
	}

	return true;
}

std::string read_message(SOCKET & ConnectSocket) {
	std::array<char, 32> recvbuf;
	std::string msg;
	do {

		int iResult = recv(ConnectSocket, recvbuf.data(), recvbuf.size(), 0);
		if (iResult > 0) {
			msg.append(recvbuf.data(), iResult);
			if (recvbuf[iResult - 1] == '\n')
			{
				return msg;
			}
		}
		 
		if (iResult < 0 ){
			std::cout << "Error: recv error -> " << WSAGetLastError();
			break;
		}

	} while (1);

	return msg;
}


void user_terminal() {
	bool ProgramRun = true;
	HANDLE handle = GetStdHandle(STD_OUTPUT_HANDLE);
	std::array<std::string, 10> msg_list;
	unsigned int i = 0;

	auto add_msg = [&](std::string & new_msg) {
		for (auto it = ++msg_list.rbegin(); it < msg_list.rend(); it++) {
			*(it - 1) = *it;
		}

		msg_list[0] = new_msg;
	};

	while (ProgramRun) {
		std::string input;
		cls(handle);
		for (auto msg : msg_list) {
			std::cout << msg;
		}
		std::cout << "\n\nEnter: ";
		std::getline(std::cin, input);

		if (input.length() > 0) {
			out_msg.push(input);
		}

		while (i < msg_list.size()) {
			auto item = in_msg.pop();
			if (item == nullptr)
				break;
			add_msg(*item.get());
		}
	}
}

SOCKET ConnectSocket = INVALID_SOCKET;

void incoming_message_handle() {
	while (1) {
		std::string temp = read_message(ConnectSocket);
		if (temp.empty()) {
			std::cout << "Error: incoming_message broke\n";
			break;
		}

		in_msg.push(temp);
	}
}

void outcoming_message_handle() {
	while (1) {
		auto msg = out_msg.pop();
		if (msg == nullptr)
			continue;

		send_message(ConnectSocket, *msg.get());
	}
}

void chat_client() {
	WSADATA wsaData;
	struct addrinfo *result = nullptr,
		*ptr = nullptr,
		hints;

	// Initialize Winsock
	int iResult = WSAStartup(MAKEWORD(2, 2), &wsaData);
	if (iResult != 0) {
		std::cout << "WSAStartup failed with error " << iResult;
		return;
	}

	ZeroMemory(&hints, sizeof(hints));
	hints.ai_family = AF_UNSPEC;
	hints.ai_socktype = SOCK_STREAM;
	hints.ai_protocol = IPPROTO_TCP;

	// Resolve the server address and port
	iResult = getaddrinfo(DEFAULT_ADDRESS.c_str(), DEFAULT_PORT, &hints, &result);
	if (iResult != 0) {
		std::cout << "getaddrinfo failed with error: " << iResult;
		WSACleanup();
		return;
	}

	// Attempt to connect to an address until one succeeds
	for (ptr = result; ptr != NULL; ptr = ptr->ai_next) {

		// Create a SOCKET for connecting to server
		ConnectSocket = socket(ptr->ai_family, ptr->ai_socktype,
			ptr->ai_protocol);
		if (ConnectSocket == INVALID_SOCKET) {
			std::cout <<" Error: socket failed with error: " <<WSAGetLastError();
			WSACleanup();
			return;
		}

		// Connect to server.
		iResult = connect(ConnectSocket, ptr->ai_addr, (int)ptr->ai_addrlen);
		if (iResult == SOCKET_ERROR) {
			closesocket(ConnectSocket);
			ConnectSocket = INVALID_SOCKET;
			continue;
		}
		break;
	}

	freeaddrinfo(result);

	if (ConnectSocket == INVALID_SOCKET) {
		std::cout << "Error: Unable to connect to server!\n";
		WSACleanup();
		return;
	}

	std::thread t1(incoming_message_handle);
	std::thread t2(outcoming_message_handle);

	user_terminal();

	// shutdown the connection since no more data will be sent
	/*iResult = shutdown(ConnectSocket, SD_SEND);
	if (iResult == SOCKET_ERROR) {
		printf("shutdown failed with error: %d\n", WSAGetLastError());
		closesocket(ConnectSocket);
		WSACleanup();
		return 1;
	}*/

	t1.join();
	t2.join();

	// cleanup
	closesocket(ConnectSocket);
	WSACleanup();
}


int main(void) {
	chat_client();
	return 0;
}