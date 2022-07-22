#include <cstring>
#include <sys/resource.h>
#include "daphne.h"
#include "vldp_common.h"

extern       struct vldp_in_info   g_local_info;
extern const struct vldp_out_info *g_vldp_info;


bool enable_core_dump(){
    struct rlimit corelim;

    corelim.rlim_cur = RLIM_INFINITY;
    corelim.rlim_max = RLIM_INFINITY;

    return (0 == setrlimit(RLIMIT_CORE, &corelim));
}

int main()
{
    enable_core_dump();
    daphne_poll();
    strcpy(g_req_file, "lair.m2v");
    // Note that this lair.m2v frame 0 is at 151
    g_req_frame = 172;
    open();

    search();
	return 0;
}