/*
 * SemiPrep.cpp
 *
 */

#include "SemiPrep.h"

template<class T>
SemiPrep<T>::SemiPrep(SubProcessor<T>* proc, DataPositions& usage) : OTPrep<T>(proc, usage)
{
    this->params.set_passive();
}

template<class T>
void SemiPrep<T>::buffer_triples()
{
    assert(this->triple_generator);
    this->triple_generator->generatePlainTriples();
    for (auto& x : this->triple_generator->plainTriples)
    {
        this->triples.push_back({{x[0], x[1], x[2]}});
    }
    this->triple_generator->unlock();
}

template<class T>
void SemiPrep<T>::buffer_inverses()
{
    assert(this->proc != 0);
    BufferPrep<T>::buffer_inverses(this->proc->MC, this->proc->P);
}
