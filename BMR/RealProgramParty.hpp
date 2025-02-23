/*
 * RealProgramParty.cpp
 *
 */

#include "RealProgramParty.h"

#include "Register_inline.h"

#include "Tools/NetworkOptions.h"
#include "Math/Setup.h"

#include "RealGarbleWire.hpp"
#include "CommonParty.hpp"
#include "Register.hpp"
#include "ProgramParty.hpp"
#include "GC/Machine.hpp"
#include "GC/Processor.hpp"
#include "GC/Program.hpp"
#include "GC/Instruction.hpp"
#include "GC/Secret.hpp"
#include "GC/Thread.hpp"
#include "GC/ThreadMaster.hpp"

template<class T>
RealProgramParty<T>* RealProgramParty<T>::singleton = 0;

template<class T>
RealProgramParty<T>::RealProgramParty(int argc, const char** argv) :
		garble_processor(garble_machine), dummy_proc({{}, 0})
{
	assert(singleton == 0);
	singleton = this;

	ez::ezOptionParser opt;
	opt.add(
			T::needs_ot ? "2" : "3", // Default.
			0, // Required?
			1, // Number of args expected.
			0, // Delimiter if expecting multiple args.
			"Number of players", // Help description.
			"-N", // Flag token.
			"--nparties" // Flag token.
	);
	opt.parse(argc, argv);
	int nparties;
	opt.get("-N")->getInt(nparties);
	this->check(nparties);

	NetworkOptions network_opts(opt, argc, argv);
	OnlineOptions online_opts(opt, argc, argv);
	assert(not online_opts.interactive);

	online_opts.finalize(opt, argc, argv);
	this->load(online_opts.progname);

	auto& N = this->N;
	auto& P = this->P;
	auto& delta = this->delta;
	auto& mac_key = this->mac_key;
	auto& garble_processor = this->garble_processor;
	auto& prng = this->prng;
	auto& program = this->program;
	auto& MC = this->MC;

	this->_id = online_opts.playerno + 1;
	Server* server = Server::start_networking(N, online_opts.playerno, nparties,
			network_opts.hostname, network_opts.portnum_base);
	if (T::needs_ot)
	    P = new PlainPlayer(N, 0);
	else
	    P = new CryptoPlayer(N, 0);

	delta = prng.get_doubleword();
#ifdef KEY_SIGNAL
	delta.set_signal(1);
#endif
#ifdef VERBOSE
	cerr << "delta: " << delta << endl;
#endif

	string prep_dir = get_prep_dir(nparties, 128, 128);
	usage = DataPositions(N.num_players());
	if (online_opts.live_prep)
	{
		mac_key.randomize(prng);
		if (T::needs_ot)
			BaseMachine::s().ot_setups.push_back({{{*P, true}}});
		prep = Preprocessing<T>::get_live_prep(0, usage);
	}
	else
	{
		Z2<64> _;
		read_mac_keys(prep_dir, online_opts.playerno, nparties, _, mac_key);
		prep = new Sub_Data_Files<T>(N, prep_dir, usage);
	}

	MC = new typename T::MAC_Check(mac_key);

	garble_processor.reset(program);
	this->processor.open_input_file(N.my_num(), 0);

	shared_proc = new SubProcessor<T>(dummy_proc, *MC, *prep, *P);

	auto& inputter = shared_proc->input;
	inputter.reset_all(*P);
	for (int i = 0; i < N.num_players(); i++)
		if (i == N.my_num())
			inputter.add_mine(int128(delta.r));
		else
			inputter.add_other(i);
	inputter.exchange();
	for (int i = 0; i < N.num_players(); i++)
		deltas.push_back(inputter.finalize(i));

	garble_inputter = new Inputter(shared_proc, *P);
	garble_protocol = new typename T::Protocol(*P);
	for (int i = 0; i < SPDZ_OP_N; i++)
		this->spdz_wires[i].push_back({});

	do
	{
		next = GC::TIME_BREAK;
		garble();
		try
		{
			this->online_timer.start();
			this->start_online_round();
			this->online_timer.stop();
		}
		catch (needs_cleaning& e)
		{
		}
	}
	while (next != GC::DONE_BREAK);

	MC->Check(*P);

	if (server)
		delete server;
}

template<class T>
void RealProgramParty<T>::garble()
{
	auto& P = this->P;
	auto& garble_processor = this->garble_processor;
	auto& program = this->program;
	auto& MC = this->MC;

	while (next == GC::TIME_BREAK)
	{
		garble_jobs.clear();
		garble_inputter->reset_all(*P);
		auto& protocol = *garble_protocol;
		protocol.init_mul(shared_proc);

		next = this->first_phase(program, garble_processor, this->garble_machine);

		garble_inputter->exchange();
		protocol.exchange();

		typename T::Protocol second_protocol(*P);
		second_protocol.init_mul(shared_proc);
		for (auto& job : garble_jobs)
			job.middle_round(*this, second_protocol);

		second_protocol.exchange();

		vector<T> wires;
		for (auto& job : garble_jobs)
			job.last_round(*this, *garble_inputter, second_protocol, wires);

		vector<typename T::clear> opened;
		MC->POpen(opened, wires, *P);

		for (auto& x : opened)
			this->garbled_circuit.serialize(x);

		this->garbled_circuits.push_and_clear(this->garbled_circuit);
		this->input_masks_store.push_and_clear(this->input_masks);
		this->output_masks_store.push_and_clear(this->output_masks);
	}
}

template<class T>
RealProgramParty<T>::~RealProgramParty()
{
	delete shared_proc;
	delete prep;
	delete garble_inputter;
	delete garble_protocol;
}

template<class T>
void RealProgramParty<T>::receive_keys(Register& reg)
{
#ifndef FREE_XOR
#error not implemented
#endif
	auto& _id = this->_id;
	auto& _N = this->_N;
	reg.init(_N);
	reg.keys[0][_id - 1] = this->prng.get_doubleword();
#ifdef KEY_SIGNAL
	reg.keys[0][_id - 1].set_signal(0);
#endif
	reg.keys[1][_id - 1] = reg.keys[0][_id - 1] ^ this->get_delta();
}

template<class T>
void RealProgramParty<T>::receive_all_keys(Register& reg, bool external)
{
	(void) reg, (void) external;
	throw not_implemented();
}

template<class T>
void RealProgramParty<T>::process_prf_output(PRFOutputs& prf_output, PRFRegister* out, const PRFRegister* left, const PRFRegister* right)
{
	assert(out != 0 and left != 0 and right != 0);
	auto l = reinterpret_cast<const RealGarbleWire<T>*>(left);
	auto r = reinterpret_cast<const RealGarbleWire<T>*>(right);
	reinterpret_cast<RealGarbleWire<T>*>(out)->garble(prf_output, *l, *r);
}

template<class T>
void RealProgramParty<T>::push_spdz_wire(SpdzOp op, const RealGarbleWire<T>& wire)
{
	DualWire<T> spdz_wire;
	spdz_wire.mask = wire.mask;
	for (int i = 0; i < 2; i++)
		spdz_wire.my_keys[i] = wire.keys[i][this->N.my_num()];
	spdz_wire.pack(this->spdz_wires[op].back());
	this->spdz_storage += sizeof(SpdzWire);
}
