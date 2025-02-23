/*
 * Register.h
 *
 */

#ifndef PROTOCOL_SRC_REGISTER_H_
#define PROTOCOL_SRC_REGISTER_H_

#include <vector>
#include <utility>
#include <stdint.h>
using namespace std;

#include "config.h"
#include "Key.h"
#include "Wire.h"
#include "GC/Clear.h"
#include "GC/Memory.h"
#include "GC/Access.h"
#include "Math/gf2n.h"
#include "Tools/FlexBuffer.h"

#ifndef FREE_XOR
#warning not using free XOR has not been tested in a while
#endif

//#define PAD_TO_8(n) (n+8-n%8)
#define PAD_TO_8(n) (n)

#ifdef N_PARTIES
#define MAX_N_PARTIES N_PARTIES
#endif

#ifdef MAX_N_PARTIES
class BaseKeyVector
{
    Key keys[MAX_N_PARTIES];
public:
    Key& operator[](int i) { return keys[i]; }
    const Key& operator[](int i) const { return keys[i]; }
    Key* data() { return keys; }
    const Key* data() const { return keys; }
#ifdef N_PARTIES
    BaseKeyVector(int n_parties = 0) { (void)n_parties; avx_memzero(keys, sizeof(keys)); }
    size_t size() const { return N_PARTIES; }
    void resize(int size) { (void)size; }
#else
    BaseKeyVector(int n_parties = 0) : n_parties(n_parties) { memset(keys, 0, sizeof(keys)); }
    size_t size() const { return n_parties; }
    void resize(int size) { n_parties = size; }
private:
    int n_parties;
#endif
};
#else
class BaseKeyVector : public vector<Key>
{
public:
	BaseKeyVector(int size = 0) : vector<Key>(size, Key(0)) {}
	void resize(int size) { vector<Key>::resize(size, Key(0)); }
};
#endif

class KeyVector : public BaseKeyVector
{
public:
	KeyVector(int size = 0) : BaseKeyVector(size) {}
	size_t byte_size() const { return size() * sizeof(Key); }
	void operator=(const KeyVector& source);
    KeyVector operator^(const KeyVector& other) const;
    template <class T>
    void serialize_no_allocate(T& output) const { output.serialize_no_allocate(data(), byte_size()); }
    template <class T>
    void serialize(T& output) const { output.serialize(data(), byte_size()); }
    void unserialize(ReceivedMsg& source, int n_parties);
    friend ostream& operator<<(ostream& os, const KeyVector& kv);
};

class GarbledGate;
class CommonParty;

template <int I>
class KeyTuple {
	friend class Register;

	static long counter;

protected:
	KeyVector keys[I];

	int part_size() { return keys[0].size() * sizeof(Key); }
public:
	KeyTuple() {}
	KeyTuple(int n_parties) { init(n_parties); }
	void init(int n_parties);
	int byte_size() { return I * keys[0].byte_size(); }
	KeyVector& operator[](int i) { return keys[i]; }
	const KeyVector& operator[](int i) const { return keys[i]; }
	KeyTuple<I> operator^(const KeyTuple<I>& other) const;
	void copy_to(Key* dest);
	void unserialize(ReceivedMsg& source, int n_parties);
	void copy_from(Key* source, int n_parties, int except);
	template <class T>
	void serialize_no_allocate(T& output) const;
	template <class T>
	void serialize(T& output) const;
	template <class T>
	void serialize(T& output, party_id_t pid) const;
	void unserialize(vector<char>& output);
	template <class T>
	void unserialize(T& output);
	void randomize();
	void reset();
	void print(int wire_id) const;
	void print(int wire_id, party_id_t pid);
};

namespace GC
{
class AuthValue;
class Mask;
class SpdzShare;
template <class T>
class Secret;
template <class T>
class Processor;
}

class Register {
protected:
	static int counter;

    KeyVector garbled_entry;
    char external;

public:
	char mask;
	KeyTuple<2> keys; /* Additional data stored per per party per wire: */
	                  /* Total of n*W*2 keys
	                   *  For every w={0,...,W}
	                   *      For every b={0,1}
	                   *          For every i={1...n}
	                   *              k^i_{w,b}
	                   *  This is helpful that the keys for specific w and b are adjacent
	                   *  for pipelining matters.
	                   */

	Register(int n_parties);

	void init(int n_parties);
	void init(int rfd, int n_parties);
	KeyVector& operator[](int i) { return keys[i]; }
    const Key& key(party_id_t i, int b) const { return keys[b][i-1]; }
    Key& key(party_id_t i, int b) { return keys[b][i-1]; }
    void set_eval_keys();
    void set_eval_keys(Key* keys, int n_parties, int except);
    const Key& external_key(party_id_t i) const { return garbled_entry[i-1]; }
    void set_external_key(party_id_t i, const Key& key);
    void reset_non_external_key(party_id_t i);
    void set_external(char ext);
    char get_external() const { check_external(); return external; }
    char get_external_no_check() const { return external; }
    void set_mask(char mask);
    int get_mask() const { check_mask(); return mask; }
    char get_mask_no_check() { return mask; }
    char get_output() { check_external(); check_mask(); return mask ^ external; }
    char get_output_no_check() { return mask ^ external; }
    const KeyVector& get_garbled_entry() const { return garbled_entry; }
    const Key& get_garbled_wire(party_id_t i) const { return garbled_entry[i-1]; }
    void print_input(int id);
    void print() const { keys.print(get_id()); }
    void check_external() const;
    void check_mask() const;
    void check_signal_key(int my_id, KeyVector& garbled_entry);

    void eval(const Register& left, const Register& right, GarbledGate& gate,
            party_id_t my_id, char* prf_output, int, int, int);

    void garble(const Register& left, const Register& right, Function func,
            Gate* gate, int g, vector<ReceivedMsg>& prf_outputs, SendBuffer& buffer);

    size_t get_id() const { return (size_t)this; }

    template <class T>
    void set_trace();
};


// this is to fake a "cout" that does nothing
class BlackHole
{
public:
	template <typename T>
	BlackHole& operator<<(T) { return *this; }
	BlackHole& operator<<(BlackHole& (*__pf)(BlackHole&)) { (void)__pf; return *this; }
};
inline BlackHole& endl(BlackHole& b) { return b; }
inline BlackHole& flush(BlackHole& b) { return b; }

class Phase
{
public:
    typedef NoMemory DynamicMemory;

	typedef BlackHole out_type;
	static const BlackHole out;

	static void check(const int128& value, word share, int128 mac)
	{ (void)value; (void)share; (void)mac; }
	static void get_dyn_mask(GC::Mask& mask, int length, int mac_length)
	{ (void)mask; (void)length; (void)mac_length; }
	template <class T>
	static void store_clear_in_dynamic(T& mem, const vector<GC::ClearWriteAccess>& accesses)
	{ (void)mem; (void)accesses; }
	static void unmask(GC::AuthValue& dest, word mask_share, int128 mac_mask_share,
			word masked, int128 masked_mac)
	{ (void)dest; (void)mask_share; (void)mac_mask_share; (void)masked; (void)masked_mac; }

	template<class T>
	static void store(NoMemory& dest,
			const vector<GC::WriteAccess<T> >& accesses)
	{ (void)dest; (void)accesses; throw runtime_error("dynamic memory not implemented"); }
	template<class T>
	static void load(vector<GC::ReadAccess<T> >& accesses,
			const NoMemory& source)
	{ (void)accesses; (void)source; throw runtime_error("dynamic memory not implemented"); }

	template <class T>
	static void andrs(T& processor, const vector<int>& args) { processor.andrs(args); }
	template <class T>
	static void ands(T& processor, const vector<int>& args) { processor.ands(args); }
	template <class T>
	static void inputb(T& processor, const vector<int>& args) { processor.input(args); }
	template <class T>
	static T get_input(int from, GC::Processor<T>& processor, int n_bits)
	{ return T::input(from, processor.get_input(n_bits), n_bits); }

	static void convcbit(Integer& dest, const GC::Clear& source)
	{ (void) dest, (void) source; throw not_implemented(); }

	void input(party_id_t from, char value = -1) { (void)from; (void)value; }
	void public_input(bool value) { (void)value; }
	void random() {}
	void output() {}
};

class ProgramRegister : public Phase, public Register
{
public:
	static Register new_reg();
	static Register tmp_reg() { return new_reg(); }
	static Register and_reg() { return new_reg(); }

	template<class T>
	static void store(NoMemory& dest,
			const vector<GC::WriteAccess<T> >& accesses) { (void)dest; (void)accesses; }
	template<class T>
	static void load(vector<GC::ReadAccess<T> >& accesses,
			const GC::Memory<GC::SpdzShare>& source) { (void)accesses; (void)source; }

	// most BMR phases don't need actual input
	template<class T>
	static T get_input(int from, GC::Processor<T>& processor, int n_bits)
	{ (void)processor; return T::input(from, 0, n_bits); }

	char get_output() { return 0; }

	ProgramRegister(const Register& reg) : Register(reg) {}
};

class PRFRegister : public ProgramRegister
{
public:
	static string name() { return "PRF"; }

	template<class T>
	static void load(vector<GC::ReadAccess<T> >& accesses,
			const NoMemory& source);

	PRFRegister(const Register& reg) : ProgramRegister(reg) {}

	void op(const PRFRegister& left, const PRFRegister& right, Function func);
	void XOR(const Register& left, const Register& right);
	void input(party_id_t from, char input = -1);
	void public_input(bool value);
	void random();
	void output();
};

class EvalRegister : public ProgramRegister
{
public:
    static string name() { return "Evaluation"; }

    typedef ostream& out_type;
    static ostream& out;

    static void check(const int128& value, word share, int128 mac);
    static void get_dyn_mask(GC::Mask& mask, int length, int mac_length);
    static void unmask(GC::AuthValue& dest, word mask_share, int128 mac_mask_share,
    		word masked, int128 masked_mac);

    template<class T, class U>
    static void store(GC::Memory<U>& dest,
    		const vector<GC::WriteAccess<T> >& accesses);
    template<class T, class U>
    static void load(vector<GC::ReadAccess<T> >& accesses,
    		const GC::Memory<U>& source);

	template <class T>
	static void andrs(T& processor, const vector<int>& args);
	template <class T>
	static void inputb(T& processor, const vector<int>& args);

	template <class T>
	static T get_input(int from, GC::Processor<T>& processor, int n_bits)
	{
		(void)from, (void)processor, (void)n_bits;
		throw runtime_error("use EvalRegister::inputb()");
	}

	static void convcbit(Integer& dest, const GC::Clear& source);

	EvalRegister(const Register& reg) : ProgramRegister(reg) {}

	void op(const ProgramRegister& left, const ProgramRegister& right, Function func);
	void XOR(const Register& left, const Register& right);

	void public_input(bool value);
	void random();
	void output();
	unsigned long long get_output() { return Register::get_output(); }

	template <class T>
	static void store_clear_in_dynamic(GC::Memory<T>& mem,
			const vector<GC::ClearWriteAccess>& accesses);
	static void check_input(long long input, int n_bits);
	void input(party_id_t from, char value = -1);
	void input_helper(char value, octetStream& os);
};

class GarbleRegister : public ProgramRegister
{
public:
	static string name() { return "Garbling"; }

	template<class T>
	static void load(vector<GC::ReadAccess<T> >& accesses,
			const NoMemory& source);

	GarbleRegister(const Register& reg) : ProgramRegister(reg) {}

	void op(const Register& left, const Register& right, Function func);
	void XOR(const Register& left, const Register& right);
        void input(party_id_t from, char value = -1);
	void public_input(bool value);
	void random();
	void output() {}
};

class RandomRegister : public ProgramRegister
{
public:
	static string name() { return "Randomization"; }

	template<class T>
	static void store(NoMemory& dest,
			const vector<GC::WriteAccess<T> >& accesses);
	template<class T>
	static void load(vector<GC::ReadAccess<T> >& accesses,
			const NoMemory& source);

	RandomRegister(const Register& reg) : ProgramRegister(reg) {}

	void randomize();

	void op(const Register& left, const Register& right, Function func);
	void XOR(const Register& left, const Register& right);

	void input(party_id_t from, char value = -1);
	void public_input(bool value);
	void random();
	void output();
};


inline Register::Register(int n_parties) :
		garbled_entry(n_parties), external(NO_SIGNAL),
		mask(NO_SIGNAL), keys(n_parties)
{
}

inline void KeyVector::unserialize(ReceivedMsg& source, int n_parties)
{
	resize(n_parties);
	source.unserialize(data(), size() * sizeof(Key));
}

template <int I>
inline void KeyTuple<I>::init(int n_parties) {
	for (int i = 0; i < I; i++)
		keys[i].resize(n_parties);
}

template<int I>
inline void KeyTuple<I>::reset()
{
	for (int i = 0; i < I; i++)
		for (size_t j = 0; j < keys[i].size(); j++)
			keys[i][j] = 0;
}

template <int I>
inline void KeyTuple<I>::unserialize(ReceivedMsg& source, int n_parties) {
	for (int b = 0; b < I; b++)
		keys[b].unserialize(source, n_parties);
}

template<int I> template <class T>
void KeyTuple<I>::serialize_no_allocate(T& output) const {
	for (int i = 0; i < I; i++)
		keys[i].serialize_no_allocate(output);
}

template<int I> template <class T>
void KeyTuple<I>::serialize(T& output) const {
	for (int i = 0; i < I; i++)
		for (unsigned int j = 0; j < keys[i].size(); j++)
			keys[i][j].serialize(output);
}

template<int I> template <class T>
void KeyTuple<I>::serialize(T& output, party_id_t pid) const {
	for (int i = 0; i < I; i++)
		keys[i][pid - 1].serialize(output);
}

template<int I> template <class T>
void KeyTuple<I>::unserialize(T& output) {
	for (int i = 0; i < I; i++)
		for (unsigned int j = 0; j < keys[i].size(); j++)
			output.unserialize(keys[i][j]);
}

#endif /* PROTOCOL_SRC_REGISTER_H_ */
