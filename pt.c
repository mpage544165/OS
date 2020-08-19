#include "pt.h"

unsigned long int page_directory[1024] __attribute__((aligned(4096)));

unsigned long int first_page_table[1024] __attribute__((aligned(4096)));

extern void load_pd(unsigned int*);
extern void enable_paging();

void install_page_directory()
{
	//set each entry to not present
	for(int i = 0; i < 1024; i++) {
	    // This sets the following flags to the pages:
	    //   Supervisor: Only kernel-mode can access them
	    //   Write Enabled: It can be both read from and written to
	    //   Not Present: The page table is not present
	    page_directory[i] = 0x00000002;
	}
	
	 
	//we will fill all 1024 entries in the table, mapping 4 megabytes
	for(unsigned int i = 0; i < 1024; i++)
	{
	    // As the address is page aligned, it will always leave 12 bits zeroed.
	    // Those bits are used by the attributes ;)
	    first_page_table[i] = (i * 0x1000) | 3; // attributes: supervisor level, read/write, present.
	}

	// attributes: supervisor level, read/write, present
	page_directory[0] = ((unsigned int)first_page_table) | 3;

	//load_pd(page_directory);
	//enable_paging();
}