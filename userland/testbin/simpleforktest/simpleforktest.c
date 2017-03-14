#include <unistd.h>
#include <test161/test161.h>


int main(void)
{
	int id = fork();
	if(id)
	{
		//printf("parent says __ id is %d\n", id);
	}
	else
	{
		//printf("please dont listen to my parent ___\n");
	}
	return 0;
}
