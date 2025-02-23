/*
 * RealGarbleWire.cpp
 *
 */

#include "RealGarbleWire.h"
#include "RealProgramParty.h"
#include "Processor/MascotPrep.h"

template<class T>
void RealGarbleWire<T>::garble(PRFOutputs& prf_output,
		const RealGarbleWire<T>& left, const RealGarbleWire<T>& right)
{
	auto& party = RealProgramParty<T>::s();
	assert(party.prep != 0);
	party.prep->get_one(DATA_BIT, mask);
	auto& inputter = *party.garble_inputter;
	int n = party.N.num_players();
	int me = party.N.my_num();
	inputter.add_from_all(int128(keys[0][me].r));
	for (int k = 0; k < 4; k++)
		for (int j = 0; j < n; j++)
			inputter.add_from_all(int128(prf_output[j].for_garbling(k).r));

	assert(party.shared_proc != 0);
	assert(party.garble_protocol != 0);
	auto& protocol = *party.garble_protocol;
	protocol.prepare_mul(left.mask, right.mask);
	GarbleJob<T> job(left.mask, right.mask, mask);
	party.garble_jobs.push_back(job);
}

template<class T>
GarbleJob<T>::GarbleJob(T lambda_u, T lambda_v, T lambda_w) :
		lambda_u(lambda_u), lambda_v(lambda_v), lambda_w(lambda_w)
{
}

template<class T>
void GarbleJob<T>::middle_round(RealProgramParty<T>& party, Protocol& second_protocol)
{
	int n = party.N.num_players();
	int me = party.N.my_num();
	assert(party.garble_protocol != 0);
	auto& protocol = *party.garble_protocol;
	lambda_uv = protocol.finalize_mul();

#ifdef DEBUG_MASK
	cout << "lambda_u " << party.MC->POpen(lambda_u, *party.P) << endl;
	cout << "lambda_v " << party.MC->POpen(lambda_v, *party.P) << endl;
	cout << "lambda_w " << party.MC->POpen(lambda_w, *party.P) << endl;
	cout << "lambda_uv " << party.MC->POpen(lambda_uv, *party.P) << endl;
#endif

	for (int alpha = 0; alpha < 2; alpha++)
		for (int beta = 0; beta < 2; beta++)
			for (int j = 0; j < n; j++)
			{
				second_protocol.prepare_mul(party.shared_delta(j),
						lambda_uv + lambda_v * alpha + lambda_u * beta
								+ T(alpha * beta, me, party.MC->get_alphai())
								+ lambda_w);
			}
}

template<class T>
void GarbleJob<T>::last_round(RealProgramParty<T>& party, Inputter& inputter,
		Protocol& second_protocol, vector<T>& wires)
{
	int n = party.N.num_players();
	auto& protocol = second_protocol;

	vector<T> base_keys;
	for (int i = 0; i < n; i++)
		base_keys.push_back(inputter.finalize(i));

	for (int k = 0; k < 4; k++)
		for (int j = 0; j < n; j++)
		{
			wires.push_back({});
			auto& wire = wires.back();
			for (int i = 0; i < n; i++)
				wire += inputter.finalize(i);
			wire += base_keys[j];
			wire += protocol.finalize_mul();
		}
}

template<class T>
void RealGarbleWire<T>::XOR(const RealGarbleWire<T>& left, const RealGarbleWire<T>& right)
{
	PRFRegister::XOR(left, right);
	mask = left.mask + right.mask;
}

template<class T>
void RealGarbleWire<T>::input(party_id_t from, char input)
{
	PRFRegister::input(from, input);
	auto& party = RealProgramParty<T>::s();
	assert(party.shared_proc != 0);
	auto& inputter = party.shared_proc->input;
	inputter.reset(from - 1);
	if (from == party.get_id())
	{
		char my_mask;
		my_mask = party.prng.get_bit();
		party.input_masks.serialize(my_mask);
		inputter.add_mine(my_mask);
		inputter.send_mine();
		mask = inputter.finalize_mine();
#ifdef DEBUG_MASK
		cout << "my mask: " << (int)my_mask << endl;
#endif
	}
	else
	{
		inputter.add_other(from - 1);
		octetStream os;
		party.P->receive_player(from - 1, os, true);
		inputter.finalize_other(from - 1, mask, os);
	}
	// important to make sure that mask is a bit
	try
	{
		mask.force_to_bit();
	}
	catch (not_implemented& e)
	{
		assert(party.P != 0);
		assert(party.MC != 0);
		auto& protocol = party.shared_proc->protocol;
		protocol.init_mul(party.shared_proc);
		protocol.prepare_mul(mask, T(1, party.P->my_num(), party.mac_key) - mask);
		protocol.exchange();
		if (party.MC->POpen(protocol.finalize_mul(), *party.P) != 0)
			throw runtime_error("input mask not a bit");
	}
#ifdef DEBUG_MASK
	cout << "shared mask: " << party.MC->POpen(mask, *party.P) << endl;
#endif
}

template<class T>
void RealGarbleWire<T>::public_input(bool value)
{
	PRFRegister::public_input(value);
	mask = {};
}

template<class T>
void RealGarbleWire<T>::random()
{
	// no need to randomize keys
	PRFRegister::public_input(0);
	auto& party = RealProgramParty<T>::s();
	assert(party.prep != 0);
	party.prep->get_one(DATA_BIT, mask);
	// this is necessary to match the fake BMR evaluation phase
	party.store_wire(*this);
	keys[0].serialize(party.wires);
}

template<class T>
void RealGarbleWire<T>::output()
{
	PRFRegister::output();
	auto& party = RealProgramParty<T>::s();
	assert(party.MC != 0);
	assert(party.P != 0);
	auto m = party.MC->POpen(mask, *party.P);
	party.output_masks.push_back(m.get_bit(0));
	party.taint();
#ifdef DEBUG_MASK
	cout << "output mask: " << m << endl;
#endif
}

template<class T>
void RealGarbleWire<T>::store(NoMemory& dest,
		const vector<GC::WriteAccess<GC::Secret<RealGarbleWire> > >& accesses)
{
	(void) dest;
	auto& party = RealProgramParty<T>::s();
	for (auto access : accesses)
		for (auto& reg : access.source.get_regs())
		{
			party.push_spdz_wire(SPDZ_STORE, reg);
		}
}

template<class T>
void RealGarbleWire<T>::load(
		vector<GC::ReadAccess<GC::Secret<RealGarbleWire> > >& accesses,
		const NoMemory& source)
{
	PRFRegister::load(accesses, source);
	auto& party = RealProgramParty<T>::s();
	assert(party.prep != 0);
	for (auto access : accesses)
		for (auto& reg : access.dest.get_regs())
		{
			party.prep->get_one(DATA_BIT, reg.mask);
			party.push_spdz_wire(SPDZ_LOAD, reg);
		}
}

template<class T>
void RealGarbleWire<T>::convcbit(Integer& dest, const GC::Clear& source)
{
	(void) source;
	auto& party = RealProgramParty<T>::s();
	party.untaint();
	dest = party.convcbit;
}
