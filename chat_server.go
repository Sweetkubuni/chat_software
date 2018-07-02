package main

import "net"
import "fmt"
import "bufio"
import "strings"
import "strconv"
import "time"
import "sync"

var users = make(map[string]net.Conn)
var registerUsers = make(map[string]string)
var discarded_guestnames = make([]string, 0)

var broadcast = make(chan string, 10)
var mutex = &sync.Mutex{}

func NewNickHandle(newNick string, oldName string, conn *net.Conn) {
	_, prs := registerUsers[newNick]

	fmt.Println(oldName + " wants to change name to " + newNick)
	if prs == false {
		mutex.Lock()
		discarded_guestnames = append(discarded_guestnames, oldName)
		users[newNick] = users[oldName]
		delete(users, oldName)
		mutex.Unlock()

		broadcast <- oldName + " is now " + newNick + "\n"
	} else {
		(*conn).Write([]byte("/password " + newNick + "\n"))
		timeoutDuration := 10 * time.Second

		for {
			(*conn).SetReadDeadline(time.Now().Add(timeoutDuration))
			message, err := bufio.NewReader((*conn)).ReadString('\n')

			if err != nil {
				(*conn).Write([]byte("/error timeout error on nickname change\n"))
				return
			}
			password := strings.TrimRight(message, "\n")
			if strings.Compare(password, registerUsers[newNick]) == 0 {
				mutex.Lock()
				discarded_guestnames = append(discarded_guestnames, oldName)
				users[newNick] = users[oldName]
				delete(users, oldName)
				mutex.Unlock()

				(*conn).Write([]byte("/notify success\n"))
				broadcast <- oldName + " is now " + newNick + "\n"
				return
			} else {
				(*conn).Write([]byte("/error Wrong Try Again!\n"))
			}
		}
	}
}

func checkRegister(newNick string, password string, conn *net.Conn) {
	_, prs := registerUsers[newNick]
	if prs == true {
		(*conn).Write([]byte("/error nickname is already registered\n"))
	} else {
		mutex.Lock()
		registerUsers[newNick] = password
		mutex.Unlock()
		(*conn).Write([]byte("/notify success\n"))
	}
}

func Broadcaster() {
	for {
		message := <-broadcast
		for _, conn := range users {
			conn.Write([]byte("/message " + message))
		}
	}
}

func main() {
	ln, err := net.Listen("tcp", ":43594")
	guest_count := 0

	if err != nil {
		fmt.Println(err)
		return
	}

	defer func() {
		ln.Close()
		fmt.Println("Listener Closed")
	}()

	fmt.Println("Notice: Running Server on Local IP")

	go Broadcaster()

	for {
		conn, _ := ln.Accept()

		fmt.Println("User with ip " + conn.RemoteAddr().String() + " has connected")
		go func() {
			var key string
			if len(discarded_guestnames) > 0 {
				mutex.Lock()
				key = discarded_guestnames[0]
				discarded_guestnames = discarded_guestnames[1:]
				mutex.Unlock()
			} else {
				key = "Guest" + strconv.Itoa(guest_count)
				guest_count = guest_count + 1
			}

			mutex.Lock()
			users[key] = conn
			mutex.Unlock()

			fmt.Println("User(" + conn.RemoteAddr().String() + ") is now " + key)

			for {
				message, _ := bufio.NewReader(conn).ReadString('\n')
				if strings.Contains(message, "/nick") {
					runes := []rune(strings.TrimRight(message, "\n"))
					NewNickHandle(string(runes[6:]), key, &conn)

				} else if strings.Contains(message, "/register") {
					runes := []rune(strings.TrimRight(message, "\n"))
					arr := strings.Fields(string(runes[11:]))
					checkRegister(arr[0], arr[1], &conn)

				} else {
					broadcast <- message
				}
			}
		}()
	}
}
