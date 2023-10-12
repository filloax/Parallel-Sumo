#include <mpi.h>
#include <stdio.h>

class Test {
private:
        static int x;
public:
        static void setX(int _x) { x = _x; }
        static int getX() { return x; }
};

int Test::x;

int main(int argc, char** argv) {

        MPI_Init(NULL, NULL);      // initialize MPI environment
        int world_size; // number of processes
        MPI_Comm_size(MPI_COMM_WORLD, &world_size);

        int world_rank; // the rank of the process
        MPI_Comm_rank(MPI_COMM_WORLD, &world_rank);

        char processor_name[MPI_MAX_PROCESSOR_NAME]; // gets the name of the processor
        int name_len;
        MPI_Get_processor_name(processor_name, &name_len);

        printf("Hello world from processor %s, rank %d out of %d processors\n",
                processor_name, world_rank, world_size);

        Test::setX(world_rank * world_rank);

        printf("Test value for %d: %d\n", world_rank, Test::getX());

        MPI_Finalize(); // finish MPI environment
}
