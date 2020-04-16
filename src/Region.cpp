
// Copyright (c) 2019, Lawrence Livermore National Security, LLC.
// Produced at the Lawrence Livermore National Laboratory
//
// This file is part of Apollo.
// OCEC-17-092
// All rights reserved.
//
// Apollo is currently developed by Chad Wood, wood67@llnl.gov, with the help
// of many collaborators.
//
// Apollo was originally created by David Beckingsale, david@llnl.gov
//
// For details, see https://github.com/LLNL/apollo.
//
// Permission is hereby granted, free of charge, to any person obtaining
// a copy of this software and associated documentation files (the "Software"),
// to deal in the Software without restriction, including without limitation
// the rights to use, copy, modify, merge, publish, distribute, sublicense,
// and/or sell copies of the Software, and to permit persons to whom the
// Software is furnished to do so, subject to the following conditions:
//
// The above copyright notice and this permission notice shall be included in
// all copies or substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
// IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
// FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
// AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
// LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
// FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
// DEALINGS IN THE SOFTWARE.

#include <iostream>
#include <fstream>
#include <iomanip>
#include <ctime>
#include <chrono>
#include <memory>
#include <utility>

#include "assert.h"

#include "apollo/Apollo.h"
#include "apollo/Region.h"
#include "apollo/Logging.h"
#include "apollo/ModelFactory.h"

#include <mpi.h>

int
Apollo::Region::getPolicyIndex(void)
{
#if VERBOSE_DEBUG
    if (not currently_inside_region) {
        fprintf(stderr, "== APOLLO: [WARNING] region->getPolicyIndex() called"
                        " while NOT inside the region. Please call"
                        " region->begin() first so the model has values to use"
                        " when selecting a policy. (region->name == %s)\n", name);
        fflush(stderr);
    }
#endif
    assert( currently_inside_region );

    int choice = model->getIndex( features );
    //ggout
    //if( !model->training ) {
    //    std::cout << "Model " << model->name << " features: [ ";
    //    for(auto &f: features)
    //        std::cout << (int)f << ", ";
    //    std::cout << "] ->  " << choice << std::endl;
    //}

#if 0
    if (choice != current_policy) {
        std::cout << "Change policy " << current_policy \
            << " -> " << choice << " region " << name \
            << " training " << model->training \
            << " features: "; \
            for(auto &f : apollo->features) \
                std::cout << (int)f << ", "; \
            std::cout << std::endl; //ggout
    } else {
        //std::cout << "No policy change for region " << name << ", policy " << current_policy << std::endl; //gout
    }
#endif
    current_policy = choice;
    //log("getPolicyIndex took ", evaluation_time_total, " seconds.\n");
    return choice;
}


Apollo::Region::Region(
        const int num_features,
        const char  *regionName,
        int          numAvailablePolicies)
    :
        num_features(num_features)
{
    apollo = Apollo::instance();
    apollo->num_policies = numAvailablePolicies;
    // TODO XXX uncomment above
    //apollo->num_policies = 2;

    strncpy(name, regionName, sizeof(name)-1 );
    name[ sizeof(name)-1 ] = '\0';

    current_policy            = -1;
    currently_inside_region   = false;

    // TODO: use best_policies to train a model for new region for which there's training data
    size_t pos = Config::APOLLO_INIT_MODEL.find(",");
    std::string model_str = Config::APOLLO_INIT_MODEL.substr(0, pos);
    if( "Static" == model_str ) {
        int policy_choice = std::stoi( Config::APOLLO_INIT_MODEL.substr( pos+1 ) );
        if( policy_choice < 0 || policy_choice >= numAvailablePolicies ) {
            std::cerr << "Invalid policy_choice " << policy_choice << std::endl;
            abort();
        }
        model = ModelFactory::createStatic( apollo->num_policies, policy_choice );
        //std::cout << "Model Static policy " << policy_choice << std::endl;
    }
    else if( "Random" == model_str ) {
        model = ModelFactory::createRandom( apollo->num_policies );
        //std::cout << "Model Random" << std::endl;
    }
    else if( "RoundRobin" == model_str ) {
        model = ModelFactory::createRoundRobin( apollo->num_policies );
        //std::cout << "Model RoundRobin" << std::endl;
    }
    else {
        std::cerr << "Invalid model env var: " + Config::APOLLO_INIT_MODEL << std::endl;
        abort();
    }

    //std::cout << "Insert region " << name << " ptr " << this << std::endl;
    const auto ret = apollo->regions.insert( { name, this } );

    assert( ret.second == true );

    return;
}

Apollo::Region::~Region()
{
    if (currently_inside_region) {
        this->end();
    }

    return;
}


void
Apollo::Region::begin()
{
#if VERBOSE_DEBUG
    if (currently_inside_region) {
        fprintf(stderr, "== APOLLO: [WARNING] region->begin() called"
                        " while already inside the region. Please call"
                        " region->end() first to avoid unintended"
                        " consequences. (region->name == %s)\n",
                        name);
        fflush(stderr);
    }
#endif

    assert( !currently_inside_region );

    currently_inside_region = true;

    // NOTE: Features are tracked globally within the process.
    //       Apollo semantics require that region.begin/end calls happen
    //       from the top-level process thread, they are not encountered
    //       within a parallel code region.
    //
    //       We update this value here in case another region used a different
    //       policy index, in case this value gets looked up between this
    //       call to region.begin and our model being newly evaluated by
    //       region.getPolicyIndex ...
    //

    current_exec_time_begin = std::chrono::steady_clock::now();
    return;
}

void
Apollo::Region::end()
{

    current_exec_time_end = std::chrono::steady_clock::now();

#if VERBOSE_DEBUG
    if (not currently_inside_region) {
        fprintf(stderr, "== APOLLO: [WARNING] region->end() called"
                        " while NOT inside the region. Please call"
                        " region->begin(step) first to avoid unintended"
                        " consequences. (region->name == %s)\n", name);
        fflush(stderr);
    }
#endif

    assert( currently_inside_region );

    currently_inside_region = false;

    double duration = std::chrono::duration<double>(current_exec_time_end - current_exec_time_begin).count();

    // TODO: reduce overhead, move time calculation to reduceBestPolicies?
    // TODO: buckets of features?
    //const int bucket = 10;
    //for(auto &it : apollo->features) {
    //    //std::cout << "feature: " << it;
    //    int idiv = int(it) / bucket;
    //    it = ( idiv + 1 )* bucket;
    //    //std::cout << " -> " << it;
    //    //std::cout << std::endl;
    //}

    auto iter = measures.find( { features, current_policy } );
    if (iter == measures.end()) {
        iter = measures.insert(std::make_pair( std::make_pair( features, current_policy ),
                std::move( std::make_unique<Apollo::Region::Measure>(1, duration) )
                ) ).first;
    } else {
        iter->second->exec_count++;
        iter->second->time_total += duration;
    }

    if (apollo->traceEnabled) {
        // TODO(cdw): extract the correct values.
        int num_threads      = -999;
        int num_elements     = current_elem_count;
        int policy_index     = current_policy;
        std::string node_id  = "localhost";
        int comm_rank        = 0;
        //for (Apollo::Feature ft : apollo->features)
        //{
        //    if (     ft.name == "policy_index") { policy_index = (int) ft.value; }
        //    else if (ft.name == "num_threads")  { num_threads  = (int) ft.value; }
        //    else if (ft.name == "num_elements") { num_elements = (int) ft.value; }
        //}

        std::chrono::duration<double, std::milli> wall_elapsed \
            = current_exec_time_end.time_since_epoch();
        double wall_time = wall_elapsed.count();

        std::chrono::duration<double, std::milli> loop_elapsed \
            = current_exec_time_end - current_exec_time_begin;
        double loop_time = loop_elapsed.count();

        std::string optional_all_feature_column = "";

        // TODO(cdw): Construct a JSON of all features to emit as an extra column,
        //            in the future when we have more features.
        // NOTE.....: This works when features are a key/value map.
        //
        //if (apollo->traceEmitAllFeatures) {
        //    if (apollo->features.size() > 0) {
        //        std::stringstream ss;
        //        ss.precision(17);
        //        ss << ",\"{";
        //        for (Apollo::Feature &ft : apollo->features) {
        //            ss << "'" << ft.name << "':'" << std::fixed << ft.value << "',";
        //        }
        //        ss.seekp(-1, ss.cur);
        //        ss << "}\"";
        //        optional_all_feature_column = ss.str();
        //    } else {
        //          optional_all_feature_column = "";
        //          optional_all_feature_column = ",\"{'none':'none'}\"";
        //    }
        //}

        // TODO(cdw): ...for now, we make a quoted CSV of the elements counts
        //            for OpenMP collapsed loops.
        if (apollo->traceEmitAllFeatures) {
            std::stringstream ssvec;
            ssvec << "\"";
            for (auto &ft : features) {
                ssvec << ft;
                if (&ft != &features.back()) {
                    ssvec << ",";
                }
            }
            ssvec << "\"";
            optional_all_feature_column += ",";
            optional_all_feature_column += ssvec.str();
        }

        Apollo::TraceLine_t \
           t = std::make_tuple(
                wall_time,
                node_id,
                comm_rank,
                name,
                policy_index,
                num_threads,
                num_elements,
                loop_time,
                optional_all_feature_column
            );

        if (apollo->traceEmitOnline) {
            apollo->writeTraceLine(t);
        } else {
            apollo->storeTraceLine(t);
        }
    } // end: if (apollo->traceEnabled)


    // TODO: re-train from regression model every region execution?
    //
    //std::cout << "=== INSERT MEASURE ===" << std::endl; \
    std::cout << name << ": " << "[ "; \
        for(auto &f : apollo->features) { \
            std::cout << (int)f << ", "; \
        } \
    std::cout << " ]: " \
        << current_policy << " = ( " << iter->second->exec_count \
        << ", " << iter->second->time_total << " ) " << std::endl; \
    std::cout << ".-" << std::endl;


    features.clear();

    return;
}

int
Apollo::Region::reduceBestPolicies(int step)
{
    //int rank; \
    MPI_Comm_rank(apollo->comm, &rank); \
    std::stringstream dbgout; \
    dbgout << "=================================" << std::endl \
        << "Rank " << rank << " Region " << name << " MEASURES "  << std::endl;
    for (auto iter_measure = measures.begin();
            iter_measure != measures.end();   iter_measure++) {

        const std::vector<float>& feature_vector = iter_measure->first.first;
        const int policy_index                   = iter_measure->first.second;
        auto                           &time_set = iter_measure->second;

        //dbgout << "features: [ "; \
            int mul = 1; \
        for(auto &f : feature_vector ) { \
            dbgout << (int)f << ", "; \
            mul *= f; \
        } \
        dbgout << " = " << mul << " ]: " \
            << "policy: " << policy_index \
            << " , count: " << time_set->exec_count \
            << " , total: " << time_set->time_total \
            << " , time_avg: " <<  ( time_set->time_total / time_set->exec_count ) << std::endl;
        assert( time_set->exec_count > 0 );
        double time_avg = ( time_set->time_total / time_set->exec_count );

        auto iter =  best_policies.find( feature_vector );
        if( iter ==  best_policies.end() ) {
            best_policies.insert( { feature_vector, { policy_index, time_avg } } );
        }
        else {
            // Key exists
            if(  best_policies[ feature_vector ].second > time_avg ) {
                best_policies[ feature_vector ] = { policy_index, time_avg };
            }
        }
    }

    //dbgout << ".-" << std::endl; \
    dbgout << "Rank " << rank << " Region " << name << " Reduce " << std::endl; \
    for( auto &b : best_policies ) { \
        dbgout << "features: [ "; \
        for(auto &f : b.first ) \
            dbgout << (int)f << ", "; \
        dbgout << "]: P:" \
            << b.second.first << " T: " << b.second.second << std::endl; \
    } \
    dbgout << ".-" << std::endl; \
    std::cout << dbgout.str(); //ggout
    //std::ofstream fout("step-" + std::to_string(step) + \
            "-rank-" + std::to_string(rank) + "-" + name + "-measures.txt"); \
    fout << dbgout.str(); \
    fout.close();

    return best_policies.size();
}

void
Apollo::Region::packMeasurements(char *buf, int size, MPI_Comm comm) {
    int pos = 0;

    int rank;
    MPI_Comm_rank( comm, &rank );

    for( auto &it : best_policies ) {
        auto &feature_vector = it.first;
        int policy_index = it.second.first;
        double time_avg = it.second.second;

        // rank
        MPI_Pack( &rank, 1, MPI_INT, buf, size, &pos, comm);
        //std::cout << "rank," << rank << " pos: " << pos << std::endl;

        // num features
        MPI_Pack( &num_features, 1, MPI_INT, buf, size, &pos, comm);
        //std::cout << "rank," << rank << " pos: " << pos << std::endl;

        // feature vector
        for (float value : feature_vector ) {
            MPI_Pack( &value, 1, MPI_FLOAT, buf, size, &pos, comm );
            //std::cout << "feature," << value << " pos: " << pos << std::endl;
        }

        // policy index
        MPI_Pack( &policy_index, 1, MPI_INT, buf, size, &pos, comm );
        //std::cout << "policy_index," << policy_index << " pos: " << pos << std::endl;
        // XXX: use 64 bytes fixed for region_name
        // region name
        MPI_Pack( name, 64, MPI_CHAR, buf, size, &pos, comm );
        //std::cout << "region_name," << name << " pos: " << pos << std::endl;
        // average time
        MPI_Pack( &time_avg, 1, MPI_DOUBLE, buf, size, &pos, comm );
        //std::cout << "time_avg," << time_avg << " pos: " << pos << std::endl;
    }

    return;
}

void
Apollo::Region::setFeature(float value)
{
    features.push_back( value );
    current_elem_count = value;
    return;
}
