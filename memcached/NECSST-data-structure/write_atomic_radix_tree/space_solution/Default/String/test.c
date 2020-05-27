#include <string.h>

int main(void)
{
	unsigned char *key = "A";
	unsigned int len;

	len = strlen(key);
	printf("len = %d\n", len);
	return 0;
}
