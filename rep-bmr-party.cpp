/*
 * rep-bmr-party.cpp
 *
 */

#include "Machines/Rep.cpp"

#include "BMR/RealProgramParty.hpp"

int main(int argc, const char** argv)
{
    RealProgramParty<Rep3Share<gf2n_long>>(argc, argv);
}
