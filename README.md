# Distributed Server/Client Files Storage
C++ project aiming to learn TCP/UDP protocol - without using boost::asio.  
Multithreaded Server and Client enabling uploading, storing and fetching files from storage. 

### Short description
Application consists of server and client nodes.  
Server nodes and client nodes communicate with each other using predefined protocol.  
Nodes collaborate with each other by creating a group. Group can consist of arbitrary number of nodes. Nodes can dynamically join and leave group. Each node of given type  offers the same set of functionalities and each node is equal to other nodes of the same type (permission-wise / priority-wise etc).  
Client provides interface enabling user to store, delete, fetch and search files in particular group.  
Server provides storage for files.

### Short list of functionalities
* Each server node provides some constant storage space
* Server nodes create a group using an IP Multicast address
* Total space available in storage changes dynamically along with server nodes joining and leaving the group
* Files stored by arbitrary server node are available to each client node in this group
* Files are not dividable - whole file is stored within single server node memory
* Files are identified by names, letter size matters
* Each client node enables adding new file or deleting stored one
* Each client node enables fetching file from storage
* Each client node enables fetching list of files currently in storage
