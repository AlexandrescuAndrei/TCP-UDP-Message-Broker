# TCP-UDP-Message-Broker

A publish-subscribe messaging system implemented in **C**, combining TCP and UDP communication inside a single event-driven server.

The application consists of a central message broker and multiple TCP subscriber clients. UDP publishers send messages to the server without maintaining persistent connections, while TCP subscribers connect using unique client identifiers and dynamically subscribe or unsubscribe from topics.

The server listens for TCP and UDP traffic on the same port and multiplexes all active descriptors using `select`. Incoming UDP datagrams are decoded according to their application-level format and forwarded to the connected TCP subscribers interested in the corresponding topic.

The project focuses on practical socket programming, application-layer protocol design, TCP stream framing, UDP datagram processing, connection management, topic subscriptions, byte-order conversions, and multiplexed network I/O.

## Client-Server Architecture

The system contains two executables:

- `server` — central message broker
- `subscriber` — interactive TCP subscriber client

The server creates both a TCP socket and a UDP socket.

Both sockets are bound to the same numerical port because TCP and UDP maintain separate transport-layer namespaces.

The TCP socket listens for incoming subscriber connections, while the UDP socket receives messages from publishers.

Subscribers maintain persistent TCP connections with the broker.

UDP publishers do not need to establish a connection before sending data. They simply transmit datagrams to the server's UDP endpoint.

This architecture combines two different transport protocols according to their strengths.

TCP provides reliable ordered communication for interactive subscribers, while UDP provides lightweight message publication without connection establishment.

## I/O Multiplexing with select

The server must simultaneously monitor several different sources of input:

- standard input
- the TCP listening socket
- the UDP socket
- every connected TCP subscriber

Instead of creating one process or thread for every client, the implementation uses `select`.

A master `fd_set` stores all descriptors currently being monitored.

Before each call to `select`, this set is copied into a temporary set because the function modifies the descriptor set supplied to it.

Once `select` returns, the server iterates through the descriptors and checks which ones are ready.

Different actions are then performed depending on the descriptor type.

If standard input becomes ready, the server checks for an `exit` command.

If the TCP listening socket becomes ready, a new subscriber connection is accepted.

If the UDP socket becomes ready, a new publication is received and processed.

If an existing subscriber socket becomes ready, the server receives either a subscription command, an unsubscribe command, or detects that the client has disconnected.

This design allows a single-threaded process to manage multiple active network connections without blocking on any particular client.

## TCP Subscriber Connections

Each subscriber connects to the server through TCP.

The client receives three command-line arguments:

- client ID
- server IP address
- server port

After creating the socket and establishing the connection, the subscriber immediately sends its client identifier to the server.

Client IDs are limited to the size expected by the protocol and are used by the broker to distinguish subscribers.

The server keeps an internal array of known clients.

For each client it stores information including:

- client identifier
- socket descriptor
- connection state
- remote address
- subscription list

If a newly connected client uses an identifier that is already associated with another active connection, the server rejects the new connection.

When a previously disconnected client connects again using the same ID, the existing client record is reused rather than creating a completely new logical subscriber.

Because the subscription list belongs to this persistent client structure, existing subscriptions remain associated with the client record across reconnections during the lifetime of the server process.

## Subscribe and Unsubscribe Commands

Subscribers interact with the broker through two main commands:

`subscribe <topic>`

and:

`unsubscribe <topic>`

The subscriber program reads commands from standard input.

A valid subscription command is serialized and sent to the server over the TCP connection.

The server parses the received command and updates the linked list of subscriptions associated with that client.

Before adding a new subscription, the server checks whether the same topic is already present.

This prevents duplicate entries in the subscription list.

Unsubscription traverses the linked list, removes the matching node, reconnects the neighboring list elements, and releases the associated memory.

This provides dynamic topic management without requiring the subscriber to reconnect whenever its interests change.

## Application-Level TCP Framing

TCP provides a reliable byte stream, but it does not preserve application message boundaries.

One call to `send` does not necessarily correspond to one call to `recv`.

A transmitted message may arrive in several pieces, or several writes may become available together.

Because of this, the project implements an explicit framing protocol on top of TCP.

Every framed message begins with a **32-bit length field** stored in network byte order.

The message contents follow immediately after the length.

The receiving side first reads the complete four-byte length field and converts it back to host byte order.

It then continues reading until the exact number of bytes belonging to that message has been received.

The project implements helper functions for this behavior:

- `send_all`
- `recv_all`
- `send_framed`
- `recv_framed`

`send_all` repeatedly calls `send` until the complete buffer has been transmitted.

`recv_all` repeatedly calls `recv` until the requested amount of data has been collected or the connection closes.

This prevents the application from incorrectly assuming that TCP preserves individual message boundaries.

## UDP Message Reception

UDP publications arrive through a separate datagram socket.

When the UDP descriptor becomes ready, the server calls `recvfrom` to obtain both the message and information about its sender.

The application-level UDP packet contains:

- a fixed-size topic field
- a data-type identifier
- a type-specific payload

The server also records the sender's IP address and UDP source port so that this information can be included in the formatted message forwarded to subscribers.

Unlike TCP, UDP preserves datagram boundaries, so each received datagram represents one complete publication.

The server parses the binary data directly from the received buffer.

## Typed UDP Payloads

UDP messages can represent several different data types.

The implementation recognizes four type identifiers:

- `INT`
- `SHORT_REAL`
- `FLOAT`
- `STRING`

Each format uses a different binary representation.

For integer messages, the payload contains a sign byte followed by an unsigned 32-bit value.

The numerical field is converted from network byte order and the sign is applied when required.

`SHORT_REAL` uses an unsigned 16-bit value interpreted with two decimal digits.

The value is converted from network byte order and divided by `100`.

The `FLOAT` representation contains:

- a sign byte
- a 32-bit unsigned numerical value
- a power byte

The numerical component is converted from network byte order and divided by `10` raised to the supplied power.

The sign byte is then applied when necessary.

String messages contain textual data directly.

After decoding the payload, the server creates a human-readable representation containing the publisher IP, publisher port, topic, type, and value.

## Topic-Based Message Distribution

Once a UDP publication has been decoded, the server iterates through the known clients.

For each client, its subscription list is examined.

If one of the stored topics matches the topic of the received UDP message, the publication is eligible for forwarding to that subscriber.

The current implementation performs **exact topic matching** using string comparison.

Wildcard subscription matching is not implemented.

When a matching subscriber is currently connected, the formatted message is transmitted through its TCP socket using the framing protocol.

The subscriber receives the complete framed message and prints it immediately.

This creates the central publish-subscribe flow:

UDP publisher → broker → matching TCP subscribers.

## Subscriber Event Loop

The subscriber application also uses `select`.

It simultaneously waits for:

- commands typed on standard input
- messages received from the server

This allows the subscriber to remain interactive while continuing to receive publications asynchronously.

When standard input becomes ready, the program parses commands such as `subscribe`, `unsubscribe`, or `exit`.

Subscription commands are framed and transmitted to the broker.

When the TCP socket becomes ready, the subscriber receives one framed message from the server and prints it.

If the server closes the TCP connection, the subscriber terminates.

This structure avoids blocking indefinitely either on user input or on network traffic.

## TCP_NODELAY

Both the server and subscriber configure their TCP sockets using `TCP_NODELAY`.

This disables Nagle's algorithm for those connections.

Nagle's algorithm normally attempts to reduce the number of small TCP packets by temporarily combining small writes.

For an interactive publish-subscribe application where commands and notifications can be relatively short, reducing this additional buffering can improve responsiveness.

The project therefore explicitly favors low-latency message delivery over minimizing the number of small TCP segments.

## Client Disconnection and Reconnection

When `recv` indicates that a TCP connection has closed, the server identifies the corresponding client.

The socket is closed and removed from the descriptor set used by `select`.

The logical client record itself is kept and marked as disconnected.

This distinction between a logical subscriber and its current TCP connection allows a client with the same identifier to reconnect later.

When the ID is seen again and the previous instance is no longer connected, the new socket is associated with the existing client structure.

The client's existing subscription linked list therefore remains available during the lifetime of the running server.

Duplicate simultaneous connections using the same ID are rejected.

## Network Byte Order

The project exchanges binary values across the network, so byte order must be handled explicitly.

Functions such as:

- `htonl`
- `ntohl`
- `htons`
- `ntohs`

are used when serializing and parsing multi-byte integers.

The length field used by TCP framing is converted with `htonl` before transmission and `ntohl` after reception.

UDP numerical payloads are similarly converted before interpretation.

Port numbers obtained from socket structures are converted using `ntohs`.

This ensures that the application behaves correctly regardless of the byte order used by the host machine.

## Socket Programming

The project uses the standard POSIX socket interface.

The server relies on functions including:

- `socket`
- `bind`
- `listen`
- `accept`
- `recv`
- `recvfrom`
- `send`
- `select`
- `setsockopt`
- `close`

The subscriber additionally uses `connect` to establish its TCP session with the broker.

`sockaddr_in` structures are used for IPv4 endpoints, while `inet_aton` and `inet_ntoa` are used when converting between textual and binary IPv4 address representations.

The server binds to `INADDR_ANY`, allowing it to receive traffic addressed to any of the host's available interfaces on the selected port.

## Data Structures and State Management

The server maintains several custom data structures.

A `client` structure stores the persistent state associated with each subscriber.

Subscriptions are represented as singly linked lists.

Each subscription node stores one topic string together with a pointer to the next node.

The project also defines a linked-list representation for pending messages.

The current implementation contains helper functions for storing and sending pending messages, but the UDP forwarding path only transmits publications to subscribers that are currently connected; offline store-and-forward delivery is not wired into that path.

This distinction is important because reconnecting clients retain their subscriptions, but disconnected clients do not currently receive every publication that occurred while they were offline.

## Server Shutdown

The server also monitors standard input through `select`.

When the `exit` command is received, it closes all currently connected subscriber sockets together with the TCP and UDP server sockets.

The process then terminates.

Subscriber clients independently support their own `exit` command, which closes the TCP connection and ends the client process.

Including standard input inside the descriptor multiplexing loop means administrative commands can be processed without requiring a separate control thread.

## Project Structure

The repository contains:

- `server.c` — TCP/UDP broker, connection management, subscriptions, UDP decoding, topic distribution, framing, and `select` multiplexing
- `subscriber.c` — interactive TCP subscriber with subscription commands and asynchronous message reception
- `Makefile` — compilation rules for both executables
- `readme.txt` — original implementation notes

The project is intentionally compact, with most networking behavior implemented directly inside the two C source files.

## Build and Run

The repository includes a `Makefile` that compiles both programs using GCC.

Running:

`make`

produces:

- `server`
- `subscriber`

The server is started with:

`./server <PORT>`

A subscriber is started with:

`./subscriber <CLIENT_ID> <SERVER_IP> <SERVER_PORT>`

For example:

`./subscriber C1 127.0.0.1 12345`

Once connected, the subscriber accepts commands such as:

`subscribe weather`

`unsubscribe weather`

`exit`

The server also accepts `exit` from its own standard input.

UDP publishers are external to this repository and must send datagrams using the binary message format expected by the server.

The Makefile provides a `clean` target that removes the generated executables and object files.

## Technologies and Concepts

- C
- Computer networks
- Client-server architecture
- Publish-subscribe messaging
- Message brokers
- TCP
- UDP
- IPv4 sockets
- POSIX sockets
- `socket`
- `bind`
- `listen`
- `accept`
- `connect`
- `send`
- `recv`
- `recvfrom`
- `select`
- I/O multiplexing
- `fd_set`
- TCP stream framing
- Length-prefixed protocols
- Partial sends
- Partial receives
- Network byte order
- `htonl`
- `ntohl`
- `htons`
- `ntohs`
- TCP_NODELAY
- Nagle's algorithm
- Topic subscriptions
- Exact topic matching
- Linked lists
- Client state management
- Client reconnection
- UDP datagram parsing
- Binary protocols
- INT / SHORT_REAL / FLOAT / STRING decoding
- Dynamic memory management
- GCC
- Makefiles
