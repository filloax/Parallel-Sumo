#include <string>


using namespace std;

struct test_t {
    const string& vehId;
    double speed;
};

struct test_t_2 {
    
}

int main(int argc, char** argv) {
    test_t s0{"ciao", 0.5};
    test_t s1{"sdkjfbadshbfadfbadsgfjadgfhds", 0.6};
    printf("Size of s0 = %lu\n", sizeof(s0));
    printf("Size of s1 = %lu\n", sizeof(s1));
}