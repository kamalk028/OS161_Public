#include <unistd.h>
#include <test161/test161.h>


int main(void)
{
printf("Testing fork ::: \n");
	int id = fork();
	if(id)
	{
		printf("Parents says: Mera Beta ki ID: %d\n", id);
	}
	else
	{
		printf("Please dont listen to my parent :::\n");
	}
	return 0;
}
