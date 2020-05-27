#include <stdio.h>
#include <string.h>

#define LOW_BIT_MASK	((0x1UL << 4) - 1)
#define HIGH_BIT_MASK	(((0x1UL << 8) - 1) ^ LOW_BIT_MASK)

int main(void)
{
	unsigned char *a = "abc";
	printf("%p\n", a);
	printf("%p\n", a + 1);
	printf("%p\n", a + 2);
	return 0;
}
