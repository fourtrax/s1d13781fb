#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <linux/fb.h>
#include <linux/fs.h>
#include <sys/mman.h>

struct BMPFileHeader
{
	unsigned short bfType;
	unsigned long  bfSize;
	unsigned short bfReserved1;
	unsigned short bfReferved2;
	unsigned long  bfOffBits;
} __attribute__((packed));

struct BMPInfoHeader
{
	unsigned long  biSize;
	long           biWidth;
	long           biHeight;
	unsigned short biPlanes;
	unsigned short biBitCount;
	unsigned long  biCompression;
	unsigned long  biSizeImage;
	long           biXPixPerMeter;
	long           biYPixPerMeter;
	unsigned long  biClrUsed;
	unsigned long  biClrImporant;
} __attribute__((packed));

struct BMPFileHeader fileHeader;
struct BMPInfoHeader infoHeader;
unsigned char *image;

int openBMP(char *filename)
{
	FILE *fp;
	long xsize;
	long imgsize;

	fp = fopen(filename, "rb");
	if(!fp)
	{
		printf("can't open bmp file.\n");
		return 0;
	}

	fread(&fileHeader, 1, sizeof(struct BMPFileHeader), fp);
	if(fileHeader.bfType != 0x4d42)
	{
		printf("this is not bmp file.\n");
		return 0;
	}

	fread(&infoHeader, 1, sizeof(struct BMPInfoHeader), fp);
	if(infoHeader.biSize != 40)
	{
		printf("OS/2 header is not supported.\n");
		return 0;
	}

	if(infoHeader.biBitCount != 24)
	{
		printf("Only 24 bits color is supported.\n");
		return 0;
	}

	xsize = infoHeader.biWidth * 3;
	if(xsize%4 != 0)
		xsize += 4 - xsize%4;
	if(infoHeader.biHeight >= 0)
		imgsize = xsize * infoHeader.biHeight;
	else
		imgsize = xsize * infoHeader.biHeight * -1;
	
	image = (unsigned char*)malloc(imgsize);
	if(!image)
	{
		printf("can't allocate memory.\n");
		return 0;
	}

	fread(image, 1, imgsize, fp); 
	fclose(fp);
	return 1;
}

unsigned long getPixel(long x, long y)
{
	long xsize;
	int index;
	unsigned long ret = 0x00000000;

	if(x < 0 || infoHeader.biWidth <= x || y < 0 || infoHeader.biHeight <= y)
		return ret;
	if(infoHeader.biHeight >= 0)
		y = infoHeader.biHeight - y - 1;
	
	xsize = infoHeader.biWidth * 3;
	if(xsize%4 != 0)
		xsize += 4 - xsize%4;
	index = xsize * y + x * 3;
	ret |= (unsigned long)image[index];
	ret |= (unsigned long)(image[index+1] << 8);
	ret |= (unsigned long)(image[index+2] << 16);

	return ret;
}

void freeBMP()
{
	free(image);
}

int main(int argc, char **argv)
{
	char *bmpfile = "sample.bmp";
	char *devfile = "/dev/fb1";
	int fd;
	int screensize;
	unsigned char *fbptr;
	long x, y;
	unsigned long p;
	struct fb_var_screeninfo vinfo;
	struct fb_fix_screeninfo finfo;

	if(argc >= 2)
		bmpfile = argv[1];
	if(argc >= 3)
		devfile = argv[2];
	if(openBMP(bmpfile) == 0)
		return 1;
	fd = open(devfile, O_RDWR);
	if(!fd)
	{
		printf("can't open device.\n");
		return 1;
	}
	if ( ioctl( fd, FBIOGET_FSCREENINFO , &finfo ) ) {
		printf("Fixed information not gotton !\n");
		return 1;
	}

	if ( ioctl( fd, FBIOGET_VSCREENINFO , &vinfo ) ) {
		printf("Variable information not gotton !");
		return 1;
	}
	printf("xres=%d\n", vinfo.xres);
	printf("yres=%d\n", vinfo.yres);
	printf("bits_per_pixel=%d\n", vinfo.bits_per_pixel);
	printf("line_length=%d\n", finfo.line_length);
	screensize = vinfo.xres*vinfo.yres*vinfo.bits_per_pixel/8;

	fbptr = (unsigned char *)mmap(0,screensize,PROT_READ | PROT_WRITE,MAP_SHARED,fd,0);
	if(fbptr == -1)
	{
		printf("can't mmap fb.\n");
		close(fd);
		return 1;
	}

	for(y=0; y < vinfo.yres; y++)
	{
		for(x=0; x < vinfo.xres; x++)
		{
			p = getPixel(x, y);
			fbptr[(y*vinfo.xres + x) * 3]     = (unsigned char)(p & 0x000000ff);
			fbptr[(y*vinfo.xres + x) * 3 + 1] = (unsigned char)((p >> 8) & 0x000000ff);
			fbptr[(y*vinfo.xres + x) * 3 + 2] = (unsigned char)((p >> 16) & 0x000000ff);
		}
	}

	munmap(fbptr,screensize);
	close(fd);
	freeBMP();
	return 0;
}
