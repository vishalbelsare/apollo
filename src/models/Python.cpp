
#include "Python.h"

#include "apollo/Apollo.h"
#include "apollo/Model.h"


// 
// ----------
//
// MODEL: This is where any INDEPENDENT variables get checked
//        and a policy decision is made.
//

#define modelName "python"
#define modelFile __FILE__

int
Apollo::Model::getIndex(void)
{
    static int choice = -1;

    // TODO: Grab the python string.
    //       Grab the independent variables and stringify them.
    //       Concatenate the strings.
    //       Execute the python.
    //       Capture the return value into (int) choice.

    return choice;
}

//
// ----------
//
// BELOW: Boilerplate code to manage instances of this model:
//

void
Apollo::Model::configure(Apollo *apollo_ptr, int numPolicies, const char *model_conf_def)
{
    apollo = apollo_ptr;
    policyCount = numPolicies;
    if (model_conf_def != NULL) {
        model_def = new std::string(model_conf_def);
    } else {
        model_def = new std::string();
    }
    return;
}

Apollo::Model::Model()
{
    iterCount = 0;
}

Apollo::Model::~Model()
{
    return;
}

extern "C" Apollo::Model* create_instance(void)
{
    return new Apollo::Model;
}


extern "C" void destroy_instance(Apollo::Model *model_ref)
{
    delete model_ref;
    return;
}


