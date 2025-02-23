/*
 * OTMultiplier.h
 *
 */

#ifndef OT_OTMULTIPLIER_H_
#define OT_OTMULTIPLIER_H_

#include <vector>
using namespace std;

#include "OT/OTExtensionWithMatrix.h"
#include "OT/OTVole.h"
#include "OT/Rectangle.h"
#include "Tools/random.h"
#include "Math/Spdz2kShare.h"

template<class T>
class NPartyTripleGenerator;
template<class T>
class OTTripleGenerator;

class MultJob
{
public:
    Dtype type;
    bool input;
    int player;
    int n_inputs;
    MultJob(Dtype type = N_DTYPE) : type(type), input(false), player(-1), n_inputs(0) {}
    MultJob(int player, int n_inputs) : type(N_DTYPE), input(true), player(player), n_inputs(n_inputs) {}
};

class OTMultiplierBase
{
public:
    pthread_t thread;
    WaitQueue<MultJob> inbox;
    WaitQueue<MultJob> outbox;

    virtual ~OTMultiplierBase() {}
    virtual void multiply() = 0;
};

template <class V, class W>
class OTMultiplierMac : public OTMultiplierBase
{
public:
    vector< vector<V> > macs;
    vector<W> input_macs;
};

template <class T>
class OTMultiplier : public OTMultiplierMac<typename T::sacri_type, typename T::mac_type>
{
protected:
    BitVector keyBits;
    vector< vector<BitVector> > senderOutput;
    vector<BitVector> receiverOutput;

    void multiplyForTriples();
    void multiplyForBits();
	virtual void multiplyForInputs(MultJob job) = 0;

    virtual void after_correlation() = 0;
    virtual void init_authenticator(const BitVector& baseReceiverInput,
            const vector< vector<BitVector> >& baseSenderInput,
            const vector<BitVector>& baseReceiverOutput) = 0;

public:
    OTTripleGenerator<T>& generator;
    int thread_num;
    OTExtensionWithMatrix rot_ext;

    OTCorrelator<Matrix<typename T::Rectangle> > otCorrelator;

    OTMultiplier(OTTripleGenerator<T>& generator, int thread_num);
    virtual ~OTMultiplier();
    void multiply();
};

template <class T>
class MascotMultiplier : public OTMultiplier<Share<T>>
{
    OTCorrelator<Matrix<typename T::Square> > auth_ot_ext;
    void after_correlation();
    void init_authenticator(const BitVector& baseReceiverInput,
            const vector< vector<BitVector> >& baseSenderInput,
            const vector<BitVector>& baseReceiverOutput);

public:
    vector<T> c_output;

    MascotMultiplier(OTTripleGenerator<Share<T>>& generator, int thread_num);

	void multiplyForInputs(MultJob job);
};

template <int K, int S>
class Spdz2kMultiplier: public OTMultiplier<Spdz2kShare<K, S>>
{
    typedef Spdz2kShare<K, S> T;

    void after_correlation();
    void init_authenticator(const BitVector& baseReceiverInput,
            const vector< vector<BitVector> >& baseSenderInput,
            const vector<BitVector>& baseReceiverOutput);

    void multiplyForInputs(MultJob job);

public:
    static const int TAU = TAU(K, S);
    static const int PASSIVE_MULT_BITS = K + S;
    static const int MAC_BITS = K + 2 * S;

    vector<Z2kRectangle<TAU, PASSIVE_MULT_BITS> > c_output;
    OTVoleBase<Z2<MAC_BITS>, Z2<S>>* mac_vole;
    OTVoleBase<Z2<K + S>, Z2<S>>* input_mac_vole;

    Spdz2kMultiplier(OTTripleGenerator<T>& generator, int thread_num);
    ~Spdz2kMultiplier();
};

template<class T>
class SemiMultiplier : public OTMultiplier<T>
{
    void multiplyForInputs(MultJob job)
    {
        (void) job;
        throw not_implemented();
    }

    void after_correlation();

    void init_authenticator(const BitVector& baseReceiverInput,
            const vector< vector<BitVector> >& baseSenderInput,
            const vector<BitVector>& baseReceiverOutput)
    {
        (void) baseReceiverInput, (void) baseReceiverOutput, (void) baseSenderInput;
    }

public:
    vector<typename T::open_type> c_output;

    SemiMultiplier(OTTripleGenerator<T>& generator, int i) :
            OTMultiplier<T>(generator, i)
    {
    }
};

#endif /* OT_OTMULTIPLIER_H_ */
