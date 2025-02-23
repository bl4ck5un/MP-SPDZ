/*
 * CommonParty.cpp
 *
 */

#include "CommonParty.h"
#include "BooleanCircuit.h"
#include "Tools/benchmarking.h"

CommonParty* CommonParty::singleton = 0;

CommonFakeParty::CommonFakeParty() :
        _node(0), buffers(TYPE_MAX)
{
	insecure("MPC emulation");
}

CommonParty::CommonParty() :
		gate_counter(0), gate_counter2(0), garbled_tbl_size(0),
		cpu_timer(CLOCK_PROCESS_CPUTIME_ID)
{
	if (singleton != 0)
		throw runtime_error("there can only be one");
	singleton = this;
	prng.ReSeed();
#ifdef DEBUG_PRNG
	octet seed[SEED_SIZE];
	memset(seed, 0, sizeof(seed));
	prng.SetSeed(seed);
#endif
	cpu_timer.start();
	timer.start();
	gf2n::init_field(128);
	mac_key.randomize(prng);
}

CommonFakeParty::~CommonFakeParty()
{
    if (_node)
        delete _node;
}

CommonParty::~CommonParty()
{
	cerr << "Total time: " << timer.elapsed() << endl;
#ifdef VERBOSE
	cerr << "Wire storage: " << 1e-9 * wires.capacity() << " GB" << endl;
	cerr << "CPU time: " << cpu_timer.elapsed() << endl;
	cerr << "First phase time: " << timers[0].elapsed() << endl;
	cerr << "Second phase time: " << timers[1].elapsed() << endl;
	cerr << "Number of gates: " << gate_counter << endl;
#endif
}

void CommonParty::check(int n_parties)
{
	(void) n_parties;
#ifdef N_PARTIES
	if (n_parties != N_PARTIES)
	    throw runtime_error("wrong number of parties");
#else
#ifdef MAX_N_PARTIES
	if (n_parties > MAX_N_PARTIES)
	    throw runtime_error("too many parties");
#endif
	_N = n_parties;
#endif // N_PARTIES
}

void CommonFakeParty::init(const char* netmap_file, int id, int n_parties)
{
	check(n_parties);
	printf("netmap_file: %s\n", netmap_file);
	if (0 == strcmp(netmap_file, LOOPBACK_STR)) {
		_node = new Node( NULL, id, this, _N + 1);
	} else {
		_node = new Node(netmap_file, id, this);
	}
}

int CommonFakeParty::init(const char* netmap_file, int id)
{
	int n_parties;
	if (string(netmap_file) != string(LOOPBACK_STR))
	{
		ifstream(netmap_file) >> n_parties;
		n_parties--;
	}
	else
		n_parties = 2;
	init(netmap_file, id, n_parties);
	return n_parties;
}

void CommonParty::reset()
{
	garbled_tbl_size = 0;
}

gate_id_t CommonParty::new_gate()
{
    gate_counter++;
    garbled_tbl_size++;
    return gate_counter;
}

void CommonParty::next_gate(GarbledGate& gate)
{
    gate_counter2++;
    gate.init_inputs(gate_counter2, _N);
}

SendBuffer& CommonFakeParty::get_buffer(MSG_TYPE type)
{
	SendBuffer& buffer = buffers[type];
	buffer.clear();
	fill_message_type(buffer, type);
#ifdef DEBUG_BUFFER
	cout << type << " buffer:";
	phex(buffers.data(), 4);
#endif
	return buffer;
}

void CommonCircuitParty::print_masks(const vector<int>& indices)
{
	vector<char> bits;
	for (auto i = indices.begin(); i != indices.end(); i++)
		bits.push_back(registers[*i].get_mask_no_check());
	print_bit_array(bits);
}

void CommonCircuitParty::print_outputs(const vector<int>& indices)
{
	vector<char> bits;
	for (auto i = indices.begin(); i != indices.end(); i++)
		bits.push_back(registers[*i].get_output_no_check());
	print_bit_array(bits);
}


void CommonCircuitParty::prepare_input_regs(party_id_t from)
{
    party_t sender = _circuit->_parties[from];
    wire_id_t s = sender.wires; //the beginning of my wires
    wire_id_t n = sender.n_wires; // number of my wires
    input_regs_queue.clear();
    input_regs_queue.push_back(_N + 1);
    (*input_regs)[from].clear();
    for (wire_id_t i = 0; i < n; i++) {
        wire_id_t w = s + i;
        (*input_regs)[from].push_back(w);
    }
}

void CommonCircuitParty::prepare_output_regs()
{
    output_regs.clear();
    for (size_t i = 0; i < _OW; i++)
        output_regs.push_back(_circuit->OutWiresStart()+i);
}
