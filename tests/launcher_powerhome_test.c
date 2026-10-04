#define main launcher_program_main
#include "../launcher/src/launcher.c"
#undef main

static void event(int fd,int type,int code,int value){
    struct input_event e={0};e.type=type;e.code=code;e.value=value;
    assert(write(fd,&e,sizeof e)==sizeof e);
}
int main(void){
    int a[2],b[2];assert(pipe(a)==0&&pipe(b)==0);
    key_fd=a[0];matrix_fd=b[0];fcntl(key_fd,F_SETFL,O_NONBLOCK);fcntl(matrix_fd,F_SETFL,O_NONBLOCK);
    HomeKeys s={0};
    event(a[1],EV_KEY,KEY_BACKSPACE,1);assert(!home_key_events(&s,0));
    assert(!home_key_events(&s,1999));assert(home_key_events(&s,2000)==2);
    event(a[1],EV_KEY,KEY_BACKSPACE,2);assert(!home_key_events(&s,5000));
    event(a[1],EV_KEY,KEY_BACKSPACE,0);assert(!home_key_events(&s,5100));
    event(b[1],EV_KEY,KEY_BACKSPACE,1);assert(!home_key_events(&s,6000));assert(!home_key_events(&s,9000));
    event(a[1],EV_KEY,KEY_BACKSPACE,1);assert(!home_key_events(&s,10000));
    event(a[1],EV_KEY,KEY_POWER,1);assert(home_key_events(&s,11000)==1);
    s=(HomeKeys){0};assert(!home_key_events(&s,15000)); /* New child, no stale hold. */
    event(a[1],EV_KEY,KEY_BACKSPACE,2);assert(!home_key_events(&s,18000));
    event(a[1],EV_KEY,KEY_BACKSPACE,1);assert(!home_key_events(&s,19000));
    event(a[1],EV_SYN,SYN_DROPPED,0);event(a[1],EV_SYN,SYN_REPORT,0);
    assert(!home_key_events(&s,22000));assert(!home_key_events(&s,25000));
    event(a[1],EV_KEY,KEY_BACKSPACE,0);event(a[1],EV_KEY,KEY_BACKSPACE,1);
    assert(!home_key_events(&s,26000));assert(home_key_events(&s,28000)==2);
    for(int i=0;i<2;i++){close(a[i]);close(b[i]);}

    char config[]="/tmp/c1max-system-menu-XXXXXX";int fd=mkstemp(config);assert(fd>=0);
    const char text[]="Store|/storage/apps/current/appstore/c1max-appstore||||updates\nDictionary|/usr/bin/mp_s300/bin/mp_s300|powerhome|||dict\nHidden app|/storage/apps/current/example/c1max-example|powerhome|||example\n";
    assert(write(fd,text,sizeof(text)-1)==sizeof(text)-1);close(fd);
    load_config(config,1);assert(napps==1&&apps[0].powerhome);assert(!strcmp(apps[0].argv[0],"/usr/bin/mp_s300/bin/mp_s300"));assert(apps[0].argv[1]==NULL);
    load_config(config,1);assert(napps==1);unlink(config);
    puts("PASS powerhome timing, repeat, cross-device isolation, new child, dropped events and Store system-entry retention");
    return launcher_logic_test();
}
