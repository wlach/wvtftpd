#include "wvtftps.h"

int main()
{
    WvTFTPs tftps("/tftpboot");

    while (tftps.isok())
    {
	if (tftps.select(-1))
	    tftps.callback();
    }
}
