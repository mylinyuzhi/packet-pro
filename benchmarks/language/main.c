#include <stdio.h>
#include <time.h>
//#include <mach/mach_time.h>
#include <unistd.h>
#include <stdlib.h>

#define NUM_ITER 10
#define NUM_PACKS 1000000

struct packet {
	int a,b,c,d,e,result;
};


/*void mach_absolute_difference(uint64_t end, uint64_t start, struct timespec *tp) {
        uint64_t difference = end - start;
        static mach_timebase_info_data_t info = {0,0};

        if (info.denom == 0)
                mach_timebase_info(&info);

        uint64_t elapsednano = difference * (info.numer / info.denom);

        tp->tv_sec = elapsednano * 1e-9;
        tp->tv_nsec = elapsednano - (tp->tv_sec * 1e9);
}*/

void popPacket(struct packet *p) {
	srand(time(NULL));
	p->a = rand();
	p->b = rand();
	p->c = rand();
	p->d = rand();
	p->e = rand();
}

void proPacket(struct packet *p) {
	int i = p->a * p->b * p->c * p->d * p->e;
	p->result = i;
}

void process_data(struct timespec *t) {
//	uint64_t start, end;
//	start = mach_absolute_time();
	int i;
	for (i = 0; i < NUM_PACKS; i++) {
		struct packet p;
		p.result = -1;
		popPacket(&p);
		proPacket(&p);
		if (p.result == -1) {
			printf("RESULT ERROR\n");
		}
	}
//	end = mach_absolute_time();
//	mach_absolute_difference(end, start, t);
}


int main(int argc, char **argv) {
	int i;
	for (i=0; i < NUM_ITER; i++) {
		struct timespec t;
		process_data(&t);
//		printf("%f seconds, %lu nanoseconds\n", (double)t.tv_sec, t.tv_nsec);
//		printf("%lu\n", t.tv_nsec / NUM_PACKS);
	}
	return 0;
}
