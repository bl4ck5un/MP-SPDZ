/*
 * TripleMachine.cpp
 *
 */

#include <OT/TripleMachine.h>
#include "OT/NPartyTripleGenerator.h"
#include "OT/OTMachine.h"
#include "OT/OTTripleSetup.h"
#include "Math/gf2n.h"
#include "Math/Setup.h"
#include "Tools/ezOptionParser.h"
#include "Math/Setup.h"
#include "Auth/fake-stuff.h"

#include "Auth/fake-stuff.hpp"

#include <iostream>
#include <fstream>
using namespace std;

void* run_ngenerator_thread(void* ptr)
{
    ((MascotGenerator*)ptr)->generate();
    return 0;
}

MascotParams::MascotParams()
{
    generateMACs = true;
    amplify = true;
    check = true;
    generateBits = false;
    timerclear(&start);
}

void MascotParams::set_passive()
{
    generateMACs = amplify = check = false;
}

TripleMachine::TripleMachine(int argc, const char** argv) :
        nConnections(1), bonding(0)
{
    opt.add(
        "1", // Default.
        0, // Required?
        1, // Number of args expected.
        0, // Delimiter if expecting multiple args.
        "Number of loops (default: 1).", // Help description.
        "-l", // Flag token.
        "--nloops" // Flag token.
    );
    opt.add(
        "", // Default.
        0, // Required?
        0, // Number of args expected.
        0, // Delimiter if expecting multiple args.
        "Generate MACs (implies -a).", // Help description.
        "-m", // Flag token.
        "--macs" // Flag token.
    );
    opt.add(
        "", // Default.
        0, // Required?
        0, // Number of args expected.
        0, // Delimiter if expecting multiple args.
        "Amplify triples.", // Help description.
        "-a", // Flag token.
        "--amplify" // Flag token.
    );
    opt.add(
        "", // Default.
        0, // Required?
        0, // Number of args expected.
        0, // Delimiter if expecting multiple args.
        "Check triples (implies -m).", // Help description.
        "-c", // Flag token.
        "--check" // Flag token.
    );
    opt.add(
        "", // Default.
        0, // Required?
        0, // Number of args expected.
        0, // Delimiter if expecting multiple args.
        "GF(p) items", // Help description.
        "-P", // Flag token.
        "--prime-field" // Flag token.
    );
    opt.add(
        "", // Default.
        0, // Required?
        0, // Number of args expected.
        0, // Delimiter if expecting multiple args.
        "Channel bonding", // Help description.
        "-b", // Flag token.
        "--bonding" // Flag token.
    );
    opt.add(
        "", // Default.
        0, // Required?
        0, // Number of args expected.
        0, // Delimiter if expecting multiple args.
        "Generate bits", // Help description.
        "-B", // Flag token.
        "--bits" // Flag token.
    );
    opt.add(
        "", // Default.
        0, // Required?
        1, // Number of args expected.
        0, // Delimiter if expecting multiple args.
        "SPDZ2k parameter, e.g., 64", // Help description.
        "-Z", // Flag token.
        "--Z2k" // Flag token.
    );
    opt.add(
        "64", // Default.
        0, // Required?
        1, // Number of args expected.
        0, // Delimiter if expecting multiple args.
        "SPDZ2k security parameter (default: 64)", // Help description.
        "-S", // Flag token.
        "--security" // Flag token.
    );

    parse_options(argc, argv);

    opt.get("-l")->getInt(nloops);
    generateBits = opt.get("-B")->isSet;
    check = opt.get("-c")->isSet || generateBits;
    generateMACs = opt.get("-m")->isSet || check;
    amplify = opt.get("-a")->isSet || generateMACs;
    primeField = opt.get("-P")->isSet;
    bonding = opt.get("-b")->isSet;
    opt.get("-Z")->getInt(z2k);
    check |= z2k;
    z2s = z2k;
    if (opt.isSet("-S"))
        opt.get("-S")->getInt(z2s);

    bigint p;
    if (output)
    {
        prep_data_dir = get_prep_dir(nplayers, 128, 128);
        ofstream outf;
        generate_online_setup(outf, prep_data_dir, p, 128, 128);
    }
    else
    {
        int idx, m;
        SPDZ_Data_Setup_Primes(p, 128, idx, m);
    }

    // doesn't work with Montgomery multiplication
    gfp1::init_field(p, false);
    gf2n::init_field(128);
    
    PRNG G;
    G.ReSeed();
    mac_key2.randomize(G);
    mac_keyp.randomize(G);
    mac_keyz.randomize(G);
}

template<class T>
NPartyTripleGenerator<T>* TripleMachine::new_generator(OTTripleSetup& setup, int i)
{
    return new NPartyTripleGenerator<T>(setup, N[i%nConnections], i, nTriplesPerThread, nloops, *this);
}

void TripleMachine::run()
{
    cout << "my_num: " << my_num << endl;
    N[0].init(my_num, 10000, "HOSTS", nplayers);
    nConnections = 1;
    if (bonding)
    {
        N[1].init(my_num, 11000, "HOSTS2", nplayers);
        nConnections = 2;
    }
    // do the base OTs
    PlainPlayer P(N[0], 0xF000);
    OTTripleSetup setup(P, true);

    vector<MascotGenerator*> generators(nthreads);
    vector<pthread_t> threads(nthreads);

    for (int i = 0; i < nthreads; i++)
    {
        if (primeField)
            generators[i] = new_generator<Share<gfp1>>(setup, i);
        else if (z2k)
        {
            if (z2k == 32 and z2s == 32)
                generators[i] = new_generator<Spdz2kShare<32, 32>>(setup, i);
            else if (z2k == 64 and z2s == 64)
                generators[i] = new_generator<Spdz2kShare<64, 64>>(setup, i);
            else if (z2k == 64 and z2s == 48)
                generators[i] = new_generator<Spdz2kShare<64, 48>>(setup, i);
            else if (z2k == 66 and z2s == 64)
                generators[i] = new_generator<Spdz2kShare<66, 64>>(setup, i);
            else if (z2k == 66 and z2s == 48)
                generators[i] = new_generator<Spdz2kShare<66, 48>>(setup, i);
            else
                throw runtime_error("not compiled for k=" + to_string(z2k) + " and s=" + to_string(z2s));
        }
        else
            generators[i] = new_generator<Share<gf2n>>(setup, i);
    }
    ntriples = generators[0]->nTriples * nthreads;
    cout <<"Setup generators\n";
    for (int i = 0; i < nthreads; i++)
    {
        // lock before starting thread to avoid race condition
        generators[i]->lock();
        pthread_create(&threads[i], 0, run_ngenerator_thread, generators[i]);
    }

    // wait for initialization, then start clock and computation
    for (int i = 0; i < nthreads; i++)
        generators[i]->wait();
    cout << "Starting computation" << endl;
    gettimeofday(&start, 0);
    for (int i = 0; i < nthreads; i++)
    {
        generators[i]->signal();
        generators[i]->unlock();
    }

    // wait for threads to finish
    for (int i = 0; i < nthreads; i++)
    {
        pthread_join(threads[i],NULL);
        cout << "thread " << i+1 << " finished\n" << flush;
    }

    map<string,Timer>& timers = generators[0]->timers;
    for (map<string,Timer>::iterator it = timers.begin(); it != timers.end(); it++)
    {
        double sum = 0;
        for (size_t i = 0; i < generators.size(); i++)
            sum += generators[i]->timers[it->first].elapsed();
        cout << it->first << " on average took time "
                << sum / generators.size() << endl;
    }

    gettimeofday(&stop, 0);
    double time = timeval_diff_in_seconds(&start, &stop);
    cout << "Time: " << time << endl;
    cout << "Throughput: " << ntriples / time << endl;

    for (size_t i = 0; i < generators.size(); i++)
        delete generators[i];

    if (output)
        output_mac_keys();
}

void TripleMachine::output_mac_keys()
{
    if (z2k) {
        write_mac_keys(prep_data_dir, my_num, nplayers, mac_keyz, mac_key2);
    }
    else
        write_mac_keys(prep_data_dir, my_num, nplayers, mac_keyp, mac_key2);
}

template<> gf2n MascotParams::get_mac_key()
{
    return mac_key2;
}

template<> gfp1 MascotParams::get_mac_key()
{
    return mac_keyp;
}

template<> Z2<48> MascotParams::get_mac_key()
{
    return mac_keyz;
}

template<> Z2<64> MascotParams::get_mac_key()
{
    return mac_keyz;
}

template<> Z2<32> MascotParams::get_mac_key()
{
    return mac_keyz;
}

template<> void MascotParams::set_mac_key(gf2n key)
{
    mac_key2 = key;
}

template<> void MascotParams::set_mac_key(gfp1 key)
{
    mac_keyp = key;
}

template<> void MascotParams::set_mac_key(Z2<64> key)
{
    mac_keyz = key;
}

template<> void MascotParams::set_mac_key(Z2<48> key)
{
    mac_keyz = key;
}
