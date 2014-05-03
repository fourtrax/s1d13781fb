#include <linux/init.h>
#include <linux/module.h>
#include <linux/device.h>
#include <linux/vmalloc.h>
#include <linux/mm.h>
#include <linux/delay.h>
#include <linux/spi/spi.h>
#include <linux/fb.h>
#include <linux/string.h>

#ifndef VM_RESERVED
#define VM_RESERVED (VM_DONTEXPAND | VM_DONTDUMP)
#endif

#define REG_BASE 0x60800
#define S1D_DISPLAY_WIDTH 480
#define S1D_DISPLAY_HEIGHT 272
#define S1D_DISPLAY_PCLK   6750000L
#define S1D_DISPLAY_BPP 24
#define VFB_SIZE_BYTES (S1D_DISPLAY_WIDTH*S1D_DISPLAY_HEIGHT*3)

#define SPI_BUFSIZE		2048
#define SPI_CMDSIZE		4
#define SPI_IMGSIZE		(SPI_BUFSIZE - SPI_CMDSIZE)

struct s1d13781fb_par
{
	/*
	int dirtyrange;
	u32 dx1, dx2, dy1, dy2;
	*/
	u8 *spi_tdata;
};

struct fb_fix_screeninfo s1d13781fb_fix =
{
	.id = "epson13781fb",
	.type = FB_TYPE_PACKED_PIXELS,
	.visual = FB_VISUAL_TRUECOLOR,
	.type_aux = 0,
	.xpanstep = 0,
	.ypanstep = 0,
	.ywrapstep = 0,
	.smem_len = VFB_SIZE_BYTES,
	.line_length = S1D_DISPLAY_WIDTH * 3,
	.accel = FB_ACCEL_NONE,
};


static int write_reg16(struct spi_device *spi, u32 addr, u16 data)
{
	u8 tdata[6];

	tdata[0] = 0x88;
	tdata[1] = (addr & 0x00070000) >> 16;
	tdata[2] = (addr & 0x0000ff00) >> 8;
	tdata[3] = (addr & 0x000000ff);
	tdata[4] = (data & 0xff00) >> 8;
	tdata[5] = (data & 0x00ff);

	return spi_write(spi, tdata, 6);
}

static int read_reg16(struct spi_device *spi, u32 addr, u16 *buff)
{
	u8 tdata[4];
	u8 rdata[4];
	int ret;

	tdata[0] = 0xc8;
	tdata[1] = (addr & 0x00070000) >> 16;
	tdata[2] = (addr & 0x0000ff00) >> 8;
	tdata[3] = (addr & 0x000000ff);

	ret = spi_write_then_read(spi, tdata, 4, rdata, 4);

	*buff = (rdata[2] << 8) | rdata[3];
	return ret;
}

static void init_lcd(struct spi_device *spi)
{
	write_reg16(spi, REG_BASE+0x12, 0x000f);
	write_reg16(spi, REG_BASE+0x14, 0x0029);
	write_reg16(spi, REG_BASE+0x10, 0x0001);
	write_reg16(spi, REG_BASE+0x16, 0x0006);
	write_reg16(spi, REG_BASE+0x20, 0x004f);

	write_reg16(spi, REG_BASE+0x24, 0x003c);
	write_reg16(spi, REG_BASE+0x26, 0x002d);
	write_reg16(spi, REG_BASE+0x28, 0x0110);
	write_reg16(spi, REG_BASE+0x2A, 0x0010);
	write_reg16(spi, REG_BASE+0x04, 0x0002);
	write_reg16(spi, REG_BASE+0x22, 0x0021);

	write_reg16(spi, REG_BASE+0x40, 0x0000);

	write_reg16(spi, REG_BASE+0x42, 0x0000);
	write_reg16(spi, REG_BASE+0x44, 0x0000);

	write_reg16(spi, REG_BASE+0x80, 0x0080);
}

static void clear_lcd(struct spi_device *spi)
{
	int timeout = 0;
	u16 reg;

	write_reg16(spi, REG_BASE+0x82, 0x000b);
	write_reg16(spi, REG_BASE+0x9a, 0x0000);
	write_reg16(spi, REG_BASE+0x9c, 0x0000);

	write_reg16(spi, REG_BASE+0x86, 0x0002);
	write_reg16(spi, REG_BASE+0x8c, 0x0000);
	write_reg16(spi, REG_BASE+0x8e, 0x0000);
	write_reg16(spi, REG_BASE+0x92, S1D_DISPLAY_WIDTH);
	write_reg16(spi, REG_BASE+0x94, S1D_DISPLAY_HEIGHT);
	write_reg16(spi, REG_BASE+0x80, 0x0001);

	while(1)
	{
		read_reg16(spi, REG_BASE+0x84, &reg);
		if(reg & 0x0001)
			break;
		if(++timeout >= 100)
		{
			//printk(KERN_INFO "clear_lcd timeout\n");
			break;
		}
		else
			udelay(10);
	}
	
}

/*
static void set_pixel(struct spi_device *spi, int x, int y, u8 r, u8 g, u8 b)
{
	u32 addr;
	u16 val1, val2, temp;

	addr = (y * S1D_DISPLAY_WIDTH + x) * 3;
	if((addr % 2) == 0)
	{
		val1 = (u16)b | ((u16)g << 8);
		val2 = (u16)r;
		write_reg16(spi, addr, val1);
		read_reg16(spi, addr + 2, &temp);
		temp = (temp & 0xff00) | val2;
		write_reg16(spi, addr + 2, temp);
	}
	else
	{
		addr &= 0xfffffffe;
		val1 = (u16)b << 8;
		val2 = ((u16)r << 8) | (u16)g;
		read_reg16(spi, addr, &temp);
		temp = (temp & 0x00ff) | val1;
		write_reg16(spi, addr, temp);
		write_reg16(spi, addr+2, val2);
	}
}
*/

static void update_buffer(struct spi_device *spi, u32 addr, u8 *buff, int size)
{
	struct fb_info *info = dev_get_drvdata(&spi->dev);
	struct s1d13781fb_par *par = (struct s1d13781fb_par*)info->par;
	u8 *tdata = par->spi_tdata;
	int txsize;

	while(size >0)
	{
		tdata[0] = 0x80;
		tdata[1] = (addr & 0x00070000) >> 16;
		tdata[2] = (addr & 0x0000ff00) >>  8;
		tdata[3] =  addr & 0x000000ff;
		txsize = (size > SPI_IMGSIZE) ? SPI_IMGSIZE : size;
		memcpy(tdata + SPI_CMDSIZE, buff, txsize);
		spi_write(spi, tdata, SPI_CMDSIZE + txsize);
		addr += txsize;
		buff += txsize;
		size -= txsize;
	}

}

static void s1d13781fb_fillrect(struct fb_info *info, const struct fb_fillrect *rect)
{

}

static void s1d13781fb_copyarea(struct fb_info *info, const struct fb_copyarea *area)
{

}

static void s1d13781fb_imageblit(struct fb_info *info, const struct fb_image *image)
{

}

static int s1d13781fb_mmap(struct fb_info *info, struct vm_area_struct *vma)
{
	unsigned long off;
	unsigned long start;
	u32 len;

	off = vma->vm_pgoff << PAGE_SHIFT;

	/* frame buffer memory */
	start = info->fix.smem_start;
	len = PAGE_ALIGN((start & ~PAGE_MASK) + info->fix.smem_len);
	if (off >= len)
	{
		return -EINVAL;
	}
	start &= PAGE_MASK;
	if ((vma->vm_end - vma->vm_start + off) > len)
		return -EINVAL;
	off += start;
	vma->vm_pgoff = off >> PAGE_SHIFT;
	/* This is an IO map - tell maydump to skip this VMA */
	vma->vm_flags |= VM_RESERVED;
	if (remap_pfn_range(vma, vma->vm_start, off >> PAGE_SHIFT, vma->vm_end - vma->vm_start, vma->vm_page_prot))
		return -EAGAIN;
	return 0;
}

static int s1d13781fb_check_var(struct fb_var_screeninfo *var, struct fb_info *info)
{
	*var = info->var;
	return 0;	   	
}

static int s1d13781fb_set_par(struct fb_info *info)
{
	return 0;
}

static struct fb_ops s1d13781fb_ops =
{
	.owner = THIS_MODULE,
	.fb_fillrect = s1d13781fb_fillrect,
	.fb_copyarea = s1d13781fb_copyarea,
	.fb_imageblit = s1d13781fb_imageblit,
	.fb_mmap = s1d13781fb_mmap,
	.fb_check_var = s1d13781fb_check_var,
	.fb_set_par = s1d13781fb_set_par,
};

static void s1d13781fb_deferred_io(struct fb_info *info, struct list_head *pagelist)
{
	struct page *cur;
	struct fb_deferred_io *fbdefio = info->fbdefio;
	struct spi_device *spi = container_of(info->device, struct spi_device, dev);
	int offset;

	if(!list_empty(pagelist))
	{
		list_for_each_entry( cur, &fbdefio->pagelist, lru )
		{
			offset = cur->index << PAGE_SHIFT;
			update_buffer(spi, offset, info->screen_base+offset, PAGE_SIZE);
		}

	}
}

static struct fb_deferred_io s1d13781fb_defio =
{
	.delay = HZ/20,
	.deferred_io = s1d13781fb_deferred_io,
};

static int s1d13781fb_probe(struct spi_device *spi)
{
	struct fb_info *info;
	struct s1d13781fb_par *par;

	printk(KERN_INFO "s1d13781fb probe\n");
	spi->bits_per_word = 8;
	spi_setup(spi);
	init_lcd(spi);
	clear_lcd(spi);


	info = framebuffer_alloc(sizeof(struct s1d13781fb_par), &spi->dev);
	if(!info)
	{
		return -ENOMEM;
	}

	info->screen_base = (char*)vmalloc(VFB_SIZE_BYTES);
	if(info->screen_base == NULL)
	{
		framebuffer_release(info);
		return -ENOMEM;
	}
	memset(info->screen_base, 0x00, VFB_SIZE_BYTES);

	par = (struct s1d13781fb_par*)info->par;
	par->spi_tdata = (u8*)kmalloc(SPI_BUFSIZE, GFP_KERNEL);
	if(!par->spi_tdata)
	{
		vfree((void*)info->screen_base);
		framebuffer_release(info);
		return -ENOMEM;
	}

	info->screen_size = VFB_SIZE_BYTES;
	info->fix = s1d13781fb_fix;
	info->fix.smem_start = virt_to_phys((void*)info->screen_base);

	info->fbops = &s1d13781fb_ops;
	info->node = -1;
	dev_set_drvdata(&spi->dev, (void*)info);

	info->fbdefio = &s1d13781fb_defio;
	fb_deferred_io_init(info);

	info->var.xres		= S1D_DISPLAY_WIDTH;
	info->var.yres		= S1D_DISPLAY_HEIGHT;
	info->var.xres_virtual	= info->var.xres;	
	info->var.yres_virtual	= info->var.yres;
	info->var.xoffset		= info->var.yoffset = 0;
	info->var.bits_per_pixel	= S1D_DISPLAY_BPP;
	info->var.grayscale		= 0;
	info->var.nonstd 		= 0;				// != 0 Non standard pixel format
	info->var.activate		= FB_ACTIVATE_NOW;		//see FB_ACTIVATE_*
	info->var.height 		= -1;			  	// height of picture in mm
	info->var.width		= -1;			   	// width of picture in mm
	info->var.accel_flags	= 0;			   	// acceleration flags (hints
	info->var.pixclock		= S1D_DISPLAY_PCLK;
	info->var.right_margin	= 0;
	info->var.lower_margin	= 0;
	info->var.hsync_len		= 0;
	info->var.vsync_len		= 0;
	info->var.left_margin	= 0;
	info->var.upper_margin	= 0;
	info->var.sync		= 0;
	info->var.vmode		= FB_VMODE_NONINTERLACED;
	info->var.red.msb_right 	= info->var.green.msb_right = info->var.blue.msb_right = 0;
	info->var.transp.offset 	= info->var.transp.length = info->var.transp.msb_right = 0;

	info->var.red.offset 	= 16;
	info->var.red.length 	= 8;
	info->var.green.offset	= 8;
	info->var.green.length	= 8;
	info->var.blue.offset	= 0;
	info->var.blue.length	= 8;
	info->var.transp.offset	= 0;
	info->var.transp.length	= 0;

	if(register_framebuffer(info) < 0)
	{
		framebuffer_release(info);
		vfree((void*)info->screen_base);
		return -EINVAL;
	}

	return 0;
}

static int s1d13781fb_remove(struct spi_device *spi)
{
	struct fb_info *info = dev_get_drvdata(&spi->dev);
	struct s1d13781fb_par *par = (struct s1d13781fb_par*)info->par;
	printk(KERN_INFO "s1d13781fb remove\n");
	
	if(info)
	{
		unregister_framebuffer(info);
		fb_deferred_io_cleanup(info);
		vfree((void*)info->screen_base);
		kfree((void*)par->spi_tdata);
		framebuffer_release(info);
	}

	return 0;
}

static struct spi_driver s1d13781fb_driver = {
	.driver = {
		.name	 = "s1d13781fb",
		.owner	= THIS_MODULE,
	},
	.probe	 = s1d13781fb_probe,
	.remove = s1d13781fb_remove,
};

module_spi_driver(s1d13781fb_driver);

MODULE_DESCRIPTION("S1D13781 SPI Framebuffer driver");
MODULE_LICENSE("GPL");
MODULE_ALIAS("spi:s1d13781fb");
