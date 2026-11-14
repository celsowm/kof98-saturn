#include "kof98/native_profile.h"
#include <stdio.h>
#include <stdlib.h>
int main(int argc,char **argv) {
 if(argc!=2)return 1;
 kof_native_profile_init(NULL,NULL);
 if(kof_native_profile_enabled())return 2;
 kof_native_profile_record(0,0,1,1);
 kof_native_profile_init(argv[1],"0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef");
 if(!kof_native_profile_enabled())return 3;
 kof_native_profile_record(0x200000,0,3,10);
 kof_native_profile_record(0x200000,1,5,20);
 kof_native_profile_record(0x200001,0,7,30);
 kof_native_profile_record(0xc21234,2,8,40);
 kof_native_profile_record(0xc01234,0,9,50);
 kof_native_profile_finish();
 return kof_native_profile_enabled()?4:0;
}
