#include <inc/lib.h>

int 
exec(const char *filename, const char **argv)
{
    int r, fd;
    uint32_t fileaddr;

    if ((r = open(filename, O_RDONLY)) < 0)
        return r;
    fd = r;
    if ((r = readelf(fd)) < 0)
        return r;
    fileaddr = (uint32_t)r;
    sys_exec(fileaddr, argv);

    // should not return
    return -1;
}


int
execl(const char *filename, const char *arg0, ...)
{
    int argc = 0;
    va_list vl;
    va_start(vl, arg0);
    while (va_arg(vl, void *) != NULL)
        argc++;
    va_end(vl);

    const char *argv[argc+3];
    argv[0] = filename;
    argv[1] = arg0;
    argv[argc+2] = NULL;

    va_start(vl, arg0);
    unsigned i;

    for (i = 0; i < argc; i++)
        argv[i+2] = va_arg(vl, const char *);
    va_end(vl);
    return exec(filename, argv);
}

