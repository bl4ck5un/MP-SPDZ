/*
 * Z2k.cpp
 *
 */

#include <Math/Z2k.h>
#include "Math/Integer.h"

template<int K>
const int Z2<K>::N_BITS;
template<int K>
const int Z2<K>::N_BYTES;

template<int K>
void Z2<K>::reqbl(int n)
{
	if (n < 0 && N_BITS != -(int)n)
	{
		throw Processor_Error(
				"Program compiled for rings of length " + to_string(-n)
				+ " but VM supports only "
				+ to_string(N_BITS));
	}
	else if (n > 0)
	{
		throw Processor_Error("Program compiled for fields not rings");
	}
}

template<int K>
bool Z2<K>::allows(Dtype dtype)
{
	return Integer::allows(dtype);
}

template<int K>
Z2<K>::Z2(const bigint& x) : Z2()
{
	auto mp = x.get_mpz_t();
	memcpy(a, mp->_mp_d, min((size_t)N_BYTES, sizeof(mp_limb_t) * abs(mp->_mp_size)));
	if (mp->_mp_size < 0)
		*this = Z2<K>() - *this;
	normalize();
}

template<int K>
Z2<K>::Z2(const Integer& x) :
        Z2((uint64_t)x.get())
{
}

template<int K>
bool Z2<K>::get_bit(int i) const
{
	return 1 & (a[i / N_LIMB_BITS] >> (i % N_LIMB_BITS));
}

template<int K>
bool Z2<K>::operator==(const Z2<K>& other) const
{
#ifdef DEBUG_MPN
	for (int i = 0; i < N_WORDS; i++)
		cout << "cmp " << hex << a[i] << " " << other.a[i] << endl;
#endif
	return mpn_cmp(a, other.a, N_WORDS) == 0;
}

template<int K>
Z2<K>& Z2<K>::invert()
{
    if (get_bit(0) != 1)
        throw division_by_zero();

    Z2<K> res = 1;
    for (int i = 0; i < K; i++)
    {
        res += Z2<K>((Z2<K>(1) - Z2<K>::Mul(*this, res)).get_bit(i)) << i;
    }
    *this = res;
    return *this;
}

template<int K>
Z2<K> Z2<K>::sqrRoot()
{
	assert(a[0] % 8 == 1);
	Z2<K> res = 1;
	for (int i = 0; i < K - 1; i++)
	{
		res += Z2<K>((*this - Z2<K>::Mul(res, res)).get_bit(i + 1)) << i;
#ifdef DEBUG_SQR
		cout << "sqr " << dec << i << " " << hex << res << " " <<  res * res << " " << (*this - res * res) << endl;
#endif
	}
	return res;
}

template<int K>
void Z2<K>::AND(const Z2<K>& x, const Z2<K>& y)
{
	mpn_and_n(a, x.a, y.a, N_WORDS);
}

template<int K>
void Z2<K>::OR(const Z2<K>& x, const Z2<K>& y)
{
	mpn_ior_n(a, x.a, y.a, N_WORDS);
}

template<int K>
void Z2<K>::XOR(const Z2<K>& x, const Z2<K>& y)
{
	mpn_xor_n(a, x.a, y.a, N_WORDS);
}

template<int K>
void Z2<K>::input(istream& s, bool human)
{
	if (human)
	{
	    s >> bigint::tmp;
	    *this = bigint::tmp;
	}
	else
	    s.read((char*)a, N_BYTES);
}

template<int K>
void Z2<K>::output(ostream& s, bool human) const
{
	if (human)
	{
	    bigint::tmp = *this;
	    s << bigint::tmp;
	}
	else
		s.write((char*)a, N_BYTES);
}

template <int K>
ostream& operator<<(ostream& o, const Z2<K>& x)
{
	x.output(o, true);
	return o;
}

#define X(N) \
	template class Z2<N>; \
	template ostream& operator<<(ostream& o, const Z2<N>& x);

X(32) X(64) X(96) X(128) X(160) X(192) X(224) X(256) X(288) X(320) X(352) X(384) X(416) X(448) X(512) X(672)
X(48) X(112) X(208)
X(114) X(130) X(162) X(194) X(324) X(388)
X(66)
X(210) X(258)
X(72)
X(106)
X(104) X(144) X(253) X(255) X(269) X(271)
