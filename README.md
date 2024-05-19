To test functionality, build server and client like :

gcc -o udp_server udp_server.c -ljansson

gcc -o udp_client udp_client.c -lpthread -ljansson

Then run firstly server:
./udp_server
then client:
./udp_client
