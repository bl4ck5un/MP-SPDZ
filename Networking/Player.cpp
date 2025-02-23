
#include "Player.h"
#include "ssl_sockets.h"
#include "Exceptions/Exceptions.h"
#include "Networking/STS.h"
#include "Tools/int.h"

#include <sys/select.h>
#include <utility>
#include <assert.h>

using namespace std;

CommsecKeysPackage::CommsecKeysPackage(vector<public_signing_key> playerpubs,
                                             secret_signing_key mypriv,
                                             public_signing_key mypub)
{
    player_public_keys = playerpubs;
    my_secret_key = mypriv;
    my_public_key = mypub;
}

void Names::init(int player,int pnb,int my_port,const char* servername)
{
  player_no=player;
  portnum_base=pnb;
  setup_names(servername, my_port);
  keys = NULL;
  setup_server();
}

void Names::init(int player,int pnb,vector<string> Nms)
{
  vector<octet*> names;
  for (auto& name : Nms)
    names.push_back((octet*)name.c_str());
  init(player, pnb, names);
}

void Names::init(int player,int pnb,vector<octet*> Nms)
{
  player_no=player;
  portnum_base=pnb;
  nplayers=Nms.size();
  names.resize(nplayers);
  setup_ports();
  for (int i=0; i<nplayers; i++) {
      names[i]=(char*)Nms[i];
  }
  keys = NULL;
  setup_server();
}

// initialize names from file, no Server.x coordination.
void Names::init(int player, int pnb, const string& filename, int nplayers_wanted)
{
  ifstream hostsfile(filename.c_str());
  if (hostsfile.fail())
  {
     stringstream ss;
     ss << "Error opening " << filename << ". See HOSTS.example for an example.";
     throw file_error(ss.str().c_str());
  }
  player_no = player;
  nplayers = 0;
  portnum_base = pnb;
  keys = NULL;
  string line;
  while (getline(hostsfile, line))
  {
    if (line.length() > 0 && line.at(0) != '#') {
      names.push_back(line);
      nplayers++;
      if (nplayers_wanted > 0 and nplayers_wanted == nplayers)
        break;
    }
  }
  if (nplayers_wanted > 0 and nplayers_wanted != nplayers)
    throw runtime_error("not enought hosts in HOSTS");
  setup_ports();
#ifdef DEBUG_NETWORKING
  cerr << "Got list of " << nplayers << " players from file: " << endl;
  for (unsigned int i = 0; i < names.size(); i++)
    cerr << "    " << names[i] << endl;
#endif
  setup_server();
}

void Names::setup_ports()
{
  ports.resize(nplayers);
  for (int i = 0; i < nplayers; i++)
    ports[i] = default_port(i);
}

void Names::set_keys( CommsecKeysPackage *keys )
{
    this->keys = keys;
}

void Names::setup_names(const char *servername, int my_port)
{
  if (my_port == DEFAULT_PORT)
    my_port = default_port(player_no);

  int socket_num;
  int pn = portnum_base - 1;
  set_up_client_socket(socket_num, servername, pn);
  send(socket_num, (octet*)&player_no, sizeof(player_no));
#ifdef DEBUG_NETWORKING
  cerr << "Sent " << player_no << " to " << servername << ":" << pn << endl;
#endif

  int inst=-1; // wait until instruction to start.
  while (inst != GO) { receive(socket_num, inst); }

  // Send my name
  octet my_name[512];
  memset(my_name,0,512*sizeof(octet));
  sockaddr_in address;
  socklen_t size = sizeof address;
  getsockname(socket_num, (sockaddr*)&address, &size);
  char* name = inet_ntoa(address.sin_addr);
  // max length of IP address with ending 0
  strncpy((char*)my_name, name, 16);
  send(socket_num,my_name,512);
  send(socket_num,(octet*)&my_port,4);
#ifdef DEBUG_NETWORKING
  fprintf(stderr, "My Name = %s\n",my_name);
  cerr << "My number = " << player_no << endl;
#endif

  // Now get the set of names
  int i;
  receive(socket_num,nplayers);
#ifdef VERBOSE
  cerr << nplayers << " players\n";
#endif
  names.resize(nplayers);
  ports.resize(nplayers);
  for (i=0; i<nplayers; i++)
    { octet tmp[512];
      receive(socket_num,tmp,512);
      names[i]=(char*)tmp;
      receive(socket_num, (octet*)&ports[i], 4);
#ifdef VERBOSE
      cerr << "Player " << i << " is running on machine " << names[i] << endl;
#endif
    }
  close_client_socket(socket_num);
}


void Names::setup_server()
{
  server = new ServerSocket(ports.at(player_no));
  server->init();
}


Names::Names(const Names& other)
{
  if (other.server != 0)
      throw runtime_error("Can copy Names only when uninitialized");
  player_no = other.player_no;
  nplayers = other.nplayers;
  portnum_base = other.portnum_base;
  names = other.names;
  ports = other.ports;
  keys = NULL;
  server = 0;
}


Names::~Names()
{
  if (server != 0)
    delete server;
}


Player::Player(const Names& Nms) :
        PlayerBase(Nms.my_num()), N(Nms)
{
  nplayers=Nms.nplayers;
  player_no=Nms.player_no;
  blk_SHA1_Init(&ctx);
}


template<class T>
MultiPlayer<T>::MultiPlayer(const Names& Nms, int id) :
        Player(Nms), send_to_self_socket(0)
{
  setup_sockets(Nms.names, Nms.ports, id, *Nms.server);
}


template<>
MultiPlayer<int>::~MultiPlayer()
{
  /* Close down the sockets */
  for (auto socket : sockets)
    close_client_socket(socket);
  close_client_socket(send_to_self_socket);
}

template<class T>
MultiPlayer<T>::~MultiPlayer()
{
}

Player::~Player()
{
}

PlayerBase::~PlayerBase()
{
#ifdef VERBOSE
  for (auto it = comm_stats.begin(); it != comm_stats.end(); it++)
    cerr << it->first << " " << 1e-6 * it->second.data << " MB in "
        << it->second.rounds << " rounds, taking " << it->second.timer.elapsed()
        << " seconds" << endl;
  if (timer.elapsed() > 0)
    cerr << "Receiving took " << timer.elapsed() << " seconds" << endl;
#endif
}



// Set up nmachines client and server sockets to send data back and fro
//   A machine is a server between it and player i if i<=my_number
//   Can also communicate with myself, but only with send_to and receive_from
template<>
void MultiPlayer<int>::setup_sockets(const vector<string>& names,const vector<int>& ports,int id_base,ServerSocket& server)
{
    sockets.resize(nplayers);
    // Set up the client side
    for (int i=player_no; i<nplayers; i++) {
        int pn=id_base+player_no;
        if (i==player_no) {
          const char* localhost = "127.0.0.1";
#ifdef DEBUG_NETWORKING
          fprintf(stderr, "Setting up send to self socket to %s:%d with id 0x%x\n",localhost,ports[i],pn);
#endif
          set_up_client_socket(sockets[i],localhost,ports[i]);
        } else {
#ifdef DEBUG_NETWORKING
          fprintf(stderr, "Setting up client to %s:%d with id 0x%x\n",names[i].c_str(),ports[i],pn);
#endif
          set_up_client_socket(sockets[i],names[i].c_str(),ports[i]);
        }
        send(sockets[i], (unsigned char*)&pn, sizeof(pn));
    }
    send_to_self_socket = sockets[player_no];
    // Setting up the server side
    for (int i=0; i<=player_no; i++) {
        int id=id_base+i;
#ifdef DEBUG_NETWORKING
        fprintf(stderr, "As a server, waiting for client with id 0x%x to connect.\n",id);
#endif
        sockets[i] = server.get_connection_socket(id);
    }

    for (int i = 0; i < nplayers; i++) {
        // timeout of 5 minutes
        struct timeval tv;
        tv.tv_sec = 300;
        tv.tv_usec = 0;
        int fl = setsockopt(sockets[i], SOL_SOCKET, SO_RCVTIMEO, (char*)&tv, sizeof(struct timeval));
        if (fl<0) { error("set_up_socket:setsockopt");  }
        socket_players[sockets[i]] = i;
    }
}


template<class T>
void MultiPlayer<T>::send_long(int i, long a) const
{
  send(sockets[i], (octet*)&a, sizeof(long));
}

template<class T>
long MultiPlayer<T>::receive_long(int i) const
{
  long res;
  receive(sockets[i], (octet*)&res, sizeof(long));
  return res;
}



void Player::send_to(int player,const octetStream& o,bool donthash) const
{
  TimeScope ts(comm_stats["Sending directly"].add(o));
  send_to_no_stats(player, o);
  if (!donthash)
    { blk_SHA1_Update(&ctx,o.get_data(),o.get_length()); }
  sent += o.get_length();
}

template<class T>
void MultiPlayer<T>::send_to_no_stats(int player,const octetStream& o) const
{
  T socket = socket_to_send(player);
  o.Send(socket);
}


template<class T>
void MultiPlayer<T>::send_all(const octetStream& o,bool donthash) const
{
  TimeScope ts(comm_stats["Sending to all"].add(o));
  for (int i=0; i<nplayers; i++)
     { if (i!=player_no)
         { o.Send(sockets[i]); }
     }
  if (!donthash)
    { blk_SHA1_Update(&ctx,o.get_data(),o.get_length()); }
  sent += o.get_length() * (num_players() - 1);
}


void Player::receive_player(int i,octetStream& o,bool donthash) const
{
  TimeScope ts(timer);
  receive_player_no_stats(i, o);
  if (!donthash)
    { blk_SHA1_Update(&ctx,o.get_data(),o.get_length()); }
}

template<class T>
void MultiPlayer<T>::receive_player_no_stats(int i,octetStream& o) const
{
  o.reset_write_head();
  o.Receive(sockets[i]);
}

void Player::receive_player(int i, FlexBuffer& buffer) const
{
  octetStream os;
  receive_player(i, os, true);
  buffer = os;
}


void Player::send_relative(const vector<octetStream>& os) const
{
  assert((int)os.size() == num_players() - 1);
  for (int offset = 1; offset < num_players(); offset++)
    send_relative(offset, os[offset - 1]);
}

void Player::send_relative(int offset, const octetStream& o) const
{
  send_to(positive_modulo(my_num() + offset, num_players()), o, true);
}

void Player::receive_relative(vector<octetStream>& os) const
{
  assert((int)os.size() == num_players() - 1);
  for (int offset = 1; offset < num_players(); offset++)
    receive_relative(offset, os[offset - 1]);
}

void Player::receive_relative(int offset, octetStream& o) const
{
  receive_player(positive_modulo(my_num() + offset, num_players()), o, true);
}

template<class T>
void MultiPlayer<T>::exchange_no_stats(int other, const octetStream& o, octetStream& to_receive) const
{
  o.exchange(sockets[other], sockets[other], to_receive);
}

void Player::exchange(int other, const octetStream& o, octetStream& to_receive) const
{
  TimeScope ts(comm_stats["Exchanging"].add(o));
  exchange_no_stats(other, o, to_receive);
  sent += o.get_length();
}


void Player::exchange(int player, octetStream& o) const
{
  exchange(player, o, o);
}

void Player::exchange_relative(int offset, octetStream& o) const
{
  exchange(get_player(offset), o);
}


template<class T>
void MultiPlayer<T>::pass_around(octetStream& o, octetStream& to_receive, int offset) const
{
  TimeScope ts(comm_stats["Passing around"].add(o));
  o.exchange(sockets.at(get_player(offset)), sockets.at(get_player(-offset)), to_receive);
  sent += o.get_length();
}


/* This is deliberately weird to avoid problems with OS max buffer
 * size getting in the way
 */
template<class T>
void MultiPlayer<T>::Broadcast_Receive(vector<octetStream>& o,bool donthash) const
{
  if (o.size() != sockets.size())
    throw runtime_error("player numbers don't match");
  TimeScope ts(comm_stats["Broadcasting"].add(o[player_no]));
  for (int i=1; i<nplayers; i++)
    {
      int send_to = (my_num() + i) % num_players();
      int receive_from = (my_num() + num_players() - i) % num_players();
      o[my_num()].exchange(sockets[send_to], sockets[receive_from], o[receive_from]);
    }
  if (!donthash)
    { for (int i=0; i<nplayers; i++)
        { blk_SHA1_Update(&ctx,o[i].get_data(),o[i].get_length()); }
    }
  sent += o[player_no].get_length() * (num_players() - 1);
}


void Player::Check_Broadcast() const
{
  if (ctx.size == 0)
    return;
  octet hashVal[HASH_SIZE];
  vector<octetStream> h(nplayers);
  blk_SHA1_Final(hashVal,&ctx);
  h[player_no].append(hashVal,HASH_SIZE);

  Broadcast_Receive(h,true);
  for (int i=0; i<nplayers; i++)
    { if (i!=player_no)
        { if (!h[i].equals(h[player_no]))
	    { throw broadcast_invalid(); }
        }
    }
  blk_SHA1_Init(&ctx);
}

template<>
void MultiPlayer<int>::wait_for_available(vector<int>& players, vector<int>& result) const
{
  fd_set rfds;
  FD_ZERO(&rfds);
  int highest = 0;
  vector<int>::iterator it;
  for (it = players.begin(); it != players.end(); it++)
    {
      if (*it >= 0)
        {
          FD_SET(sockets[*it], &rfds);
          highest = max(highest, sockets[*it]);
        }
    }

  int res = select(highest + 1, &rfds, 0, 0, 0);

  if (res < 0)
    error("select()");

  result.clear();
  result.reserve(res);
  for (it = players.begin(); it != players.end(); it++)
    {
      if (res == 0)
        break;

      if (*it >= 0 && FD_ISSET(sockets[*it], &rfds))
        {
          res--;
          result.push_back(*it);
        }
    }
}


ThreadPlayer::ThreadPlayer(const Names& Nms, int id_base) : PlainPlayer(Nms, id_base)
{
  for (int i = 0; i < Nms.num_players(); i++)
    {
      receivers.push_back(new Receiver(sockets[i]));
      receivers[i]->start();

      senders.push_back(new Sender(socket_to_send(i)));
      senders[i]->start();
    }
}

ThreadPlayer::~ThreadPlayer()
{
  for (unsigned int i = 0; i < receivers.size(); i++)
    {
      receivers[i]->stop();
      if (receivers[i]->timer.elapsed() > 0)
        cerr << "Waiting for receiving from " << i << ": " << receivers[i]->timer.elapsed() << endl;
      delete receivers[i];
    }

  for (unsigned int i = 0; i < senders.size(); i++)
    {
      senders[i]->stop();
      if (senders[i]->timer.elapsed() > 0)
        cerr << "Waiting for sending to " << i << ": " << senders[i]->timer.elapsed() << endl;
      delete senders[i];
    }
}

void ThreadPlayer::request_receive(int i, octetStream& o) const
{
  receivers[i]->request(o);
}

void ThreadPlayer::wait_receive(int i, octetStream& o, bool donthash) const
{
  (void) donthash;
  receivers[i]->wait(o);
}

void ThreadPlayer::receive_player_no_stats(int i, octetStream& o) const
{
  request_receive(i, o);
  wait_receive(i, o);
}

void ThreadPlayer::send_all(const octetStream& o,bool donthash) const
{
  for (int i=0; i<nplayers; i++)
     { if (i!=player_no)
         senders[i]->request(o);
     }

  if (!donthash)
    { blk_SHA1_Update(&ctx,o.get_data(),o.get_length()); }

  for (int i = 0; i < nplayers; i++)
    if (i != player_no)
      senders[i]->wait(o);
}


RealTwoPartyPlayer::RealTwoPartyPlayer(const Names& Nms, int other_player, int id) :
        TwoPartyPlayer(Nms.my_num()), other_player(other_player)
{
  is_server = Nms.my_num() > other_player;
  setup_sockets(other_player, Nms, Nms.ports[other_player], id);
}

RealTwoPartyPlayer::~RealTwoPartyPlayer()
{
  for(size_t i=0; i < my_secret_key.size(); i++) {
      my_secret_key[i] = 0;
  }
  close_client_socket(socket);
}

static pair<keyinfo,keyinfo> sts_initiator(int socket, CommsecKeysPackage *keys, int other_player)
{
  sts_msg1_t m1;
  sts_msg2_t m2;
  sts_msg3_t m3;
  octetStream socket_stream;

  // Start Station to Station Protocol
  STS ke(&keys->player_public_keys[other_player][0], &keys->my_public_key[0], &keys->my_secret_key[0]);
  m1 = ke.send_msg1();
  socket_stream.reset_write_head();
  socket_stream.append(m1.bytes, sizeof m1.bytes);
  socket_stream.Send(socket);
  socket_stream.Receive(socket);
  socket_stream.consume(m2.pubkey, sizeof m2.pubkey);
  socket_stream.consume(m2.sig, sizeof m2.sig);
  m3 = ke.recv_msg2(m2);
  socket_stream.reset_write_head();
  socket_stream.append(m3.bytes, sizeof m3.bytes);
  socket_stream.Send(socket);

  // Use results of STS to generate send and receive keys.
  vector<unsigned char> sendKey = ke.derive_secret(crypto_secretbox_KEYBYTES);
  vector<unsigned char> recvKey = ke.derive_secret(crypto_secretbox_KEYBYTES);
  keyinfo sendkeyinfo = make_pair(sendKey,0);
  keyinfo recvkeyinfo = make_pair(recvKey,0);
  return make_pair(sendkeyinfo,recvkeyinfo);
}

static pair<keyinfo,keyinfo> sts_responder(int socket, CommsecKeysPackage *keys, int other_player)
    // secret_signing_key mykey, public_signing_key mypubkey, public_signing_key theirkey)
{
  sts_msg1_t m1;
  sts_msg2_t m2;
  sts_msg3_t m3;
  octetStream socket_stream;

  // Start Station to Station Protocol for the responder
  STS ke(&keys->player_public_keys[other_player][0], &keys->my_public_key[0], &keys->my_secret_key[0]);
  socket_stream.Receive(socket);
  socket_stream.consume(m1.bytes, sizeof m1.bytes);
  m2 = ke.recv_msg1(m1);
  socket_stream.reset_write_head();
  socket_stream.append(m2.pubkey, sizeof m2.pubkey);
  socket_stream.append(m2.sig, sizeof m2.sig);
  socket_stream.Send(socket);
  socket_stream.Receive(socket);
  socket_stream.consume(m3.bytes, sizeof m3.bytes);
  ke.recv_msg3(m3);

  // Use results of STS to generate send and receive keys.
  vector<unsigned char> recvKey = ke.derive_secret(crypto_secretbox_KEYBYTES);
  vector<unsigned char> sendKey = ke.derive_secret(crypto_secretbox_KEYBYTES);
  keyinfo sendkeyinfo = make_pair(sendKey,0);
  keyinfo recvkeyinfo = make_pair(recvKey,0);
  return make_pair(sendkeyinfo,recvkeyinfo);
}

void RealTwoPartyPlayer::setup_sockets(int other_player, const Names &nms, int portNum, int id)
{
    id += 0xF << 28;
    const char *hostname = nms.names[other_player].c_str();
    ServerSocket *server = nms.server;
    if (is_server) {
#ifdef DEBUG_NETWORKING
        fprintf(stderr, "Setting up server with id %d\n",id);
#endif
        socket = server->get_connection_socket(id);
        if(NULL != nms.keys) {
            pair<keyinfo,keyinfo> send_recv_pair = sts_responder(socket, nms.keys, other_player);
            player_send_key = send_recv_pair.first;
            player_recv_key = send_recv_pair.second;
        }
    }
    else {
#ifdef DEBUG_NETWORKING
        fprintf(stderr, "Setting up client to %s:%d with id %d\n", hostname, portNum, id);
#endif
        set_up_client_socket(socket, hostname, portNum);
        ::send(socket, (unsigned char*)&id, sizeof(id));
        if(NULL != nms.keys) {
            pair<keyinfo,keyinfo> send_recv_pair = sts_initiator(socket, nms.keys, other_player);
            player_send_key = send_recv_pair.first;
            player_recv_key = send_recv_pair.second;
        }
    }
    p2pcommsec = (0 != nms.keys);
}

int RealTwoPartyPlayer::other_player_num() const
{
  return other_player;
}

void RealTwoPartyPlayer::send(octetStream& o)
{
  if(p2pcommsec) {
    o.encrypt_sequence(&player_send_key.first[0], player_send_key.second);
    player_send_key.second++;
  }
  TimeScope ts(comm_stats["Sending one-to-one"].add(o));
  o.Send(socket);
  sent += o.get_length();
}

void VirtualTwoPartyPlayer::send(octetStream& o)
{
  TimeScope ts(comm_stats["Sending one-to-one"].add(o));
  P.send_to_no_stats(other_player, o);
  sent += o.get_length();
}

void RealTwoPartyPlayer::receive(octetStream& o)
{
  TimeScope ts(timer);
  o.reset_write_head();
  o.Receive(socket);
  if(p2pcommsec) {
    o.decrypt_sequence(&player_recv_key.first[0], player_recv_key.second);
    player_recv_key.second++;
  }
}

void VirtualTwoPartyPlayer::receive(octetStream& o)
{
  TimeScope ts(timer);
  P.receive_player_no_stats(other_player, o);
}

void RealTwoPartyPlayer::send_receive_player(vector<octetStream>& o)
{
  {
    if (is_server)
    {
      send(o[0]);
      receive(o[1]);
    }
    else
    {
      receive(o[1]);
      send(o[0]);
    }
  }
}

void RealTwoPartyPlayer::exchange(octetStream& o) const
{
  TimeScope ts(comm_stats["Exchanging one-to-one"].add(o));
  sent += o.get_length();
  o.exchange(socket, socket);
}

void VirtualTwoPartyPlayer::send_receive_player(vector<octetStream>& o)
{
  TimeScope ts(comm_stats["Exchanging one-to-one"].add(o[0]));
  sent += o[0].get_length();
  P.exchange_no_stats(other_player, o[0], o[1]);
}

VirtualTwoPartyPlayer::VirtualTwoPartyPlayer(Player& P, int other_player) :
    TwoPartyPlayer(P.my_num()), P(P), other_player(other_player)
{
}

void OffsetPlayer::send_receive_player(vector<octetStream>& o)
{
  P.exchange(P.get_player(offset), o[0], o[1]);
}

CommStats& CommStats::operator +=(const CommStats& other)
{
  data += other.data;
  return *this;
}

NamedCommStats& NamedCommStats::operator +=(const NamedCommStats& other)
{
  for (auto it = other.begin(); it != other.end(); it++)
    (*this)[it->first] += it->second;
  return *this;
}

size_t NamedCommStats::total_data()
{
  size_t res = 0;
  for (auto& x : *this)
    res += x.second.data;
  return res;
}

template class MultiPlayer<int>;
template class MultiPlayer<ssl_socket*>;
