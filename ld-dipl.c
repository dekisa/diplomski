/* ld_dipl.c */

#include <linux/init.h>
#include <linux/module.h>
#include <linux/io.h>
#include <linux/interrupt.h>
#include <linux/uaccess.h>
#include <linux/platform_device.h>
#include <linux/of.h>

#define DEVICE_FILE_NAME	"ld_dipl"
#define DRIVER_NAME		"lddipldrv"

#define LED_OFFSET		0
#define KEYS_OFFSET		16
#define DATA_OFFSET		0
#define INTERRUPT_MASK_OFFSET	8
#define EDGE_CAPTURE_OFFSET	12

/* prototypes */
static struct platform_driver ld_dipl_driver;

/* globals */
static int g_ld_dipl_driver_irq;
static void *g_ld_dipl_driver_base_addr;
static int g_driver_mem_base_addr;
static int g_driver_mem_size;

static ssize_t irq_mask_show(struct device_driver *driver, char *buf)
{	
	uint32_t value;
	value = ioread32(g_ld_dipl_driver_base_addr + KEYS_OFFSET
			 + INTERRUPT_MASK_OFFSET);
	return scnprintf(buf, PAGE_SIZE, "mask = %u\n", value);
}

static ssize_t irq_mask_store(struct device_driver *driver, const char *buf,
			      size_t count)
{
	uint32_t value;
	sscanf(buf, "%u", &value);
	iowrite32(value,g_ld_dipl_driver_base_addr + KEYS_OFFSET
		+ INTERRUPT_MASK_OFFSET);
	return count;
}

DRIVER_ATTR(irq_mask, (S_IWUSR | S_IRUSR), irq_mask_show, irq_mask_store);

static ssize_t irq_flag_show(struct device_driver *driver, char *buf)
{
	uint32_t value;
	value = ioread32(g_ld_dipl_driver_base_addr + KEYS_OFFSET
			+ EDGE_CAPTURE_OFFSET);
	return scnprintf(buf, PAGE_SIZE, "flags = %u\n", value);
}

static ssize_t irq_flag_store(struct device_driver *driver, const char *buf,
			      size_t count)
{
	uint32_t value;
	sscanf(buf, "%u", &value);
	iowrite32(value,g_ld_dipl_driver_base_addr + KEYS_OFFSET
		+ EDGE_CAPTURE_OFFSET);
	return count;
}

DRIVER_ATTR(irq_flag, (S_IWUSR | S_IRUSR), irq_flag_show, irq_flag_store);

static ssize_t keys_show(struct device_driver *driver, char *buf)
{
	uint32_t value;
	value = ioread32(g_ld_dipl_driver_base_addr + KEYS_OFFSET
			+ DATA_OFFSET);
	return scnprintf(buf, PAGE_SIZE, "keys = %u\n", value);
}

DRIVER_ATTR(keys, (S_IRUSR), keys_show, NULL);

static ssize_t leds_store(struct device_driver *driver, const char *buf,
			      size_t count)
{
	uint32_t value;
	sscanf(buf, "%u", &value);
	iowrite32(value,g_ld_dipl_driver_base_addr + LED_OFFSET + DATA_OFFSET);
	return count;
}

DRIVER_ATTR(leds, (S_IWUSR), NULL, leds_store);

static irqreturn_t ld_dipl_isr(int irq, void *data)
{
	uint32_t value;
	
	value = ioread32(g_ld_dipl_driver_base_addr + KEYS_OFFSET
			+ EDGE_CAPTURE_OFFSET);

	pr_info("irq received: %u\n", value);
	
	iowrite32(value, g_ld_dipl_driver_base_addr + KEYS_OFFSET
		+ EDGE_CAPTURE_OFFSET);

	return IRQ_HANDLED;
}

static struct of_device_id ld_dipl_of_match[] = {
	{
	.compatible = "ld,dipl"
	},
	{ /* end of table */ }
};

MODULE_DEVICE_TABLE(of, ld_dipl_of_match);

static int ld_dipl_probe(struct platform_device *pdev)
{
	int ret;
	struct resource *res;
	struct resource *driver_mem_region;

	pr_info("probe enter\n");

	ret = -EINVAL;
	
	/* get memory resource */
	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	if (res == NULL) {
		pr_err("IORESOURCE_MEM, 0 does not exist\n");
		return ret;
	}

	g_driver_mem_base_addr = res->start;
	g_driver_mem_size = resource_size(res);

	/* reserve our memory region */
	driver_mem_region = request_mem_region(g_driver_mem_base_addr,
						    g_driver_mem_size,
						    "demo_driver_hw_region");
	if (driver_mem_region == NULL) {
		pr_err("request_mem_region failed\n");
		return ret;
	}

	/* ioremap memory region */
	g_ld_dipl_driver_base_addr = ioremap(g_driver_mem_base_addr, g_driver_mem_size);
	if (g_ld_dipl_driver_base_addr == NULL) {
		pr_err("ioremap failed\n");
		goto bad_exit_release_mem_region;
	}

	/* get interrupt resource */
	g_ld_dipl_driver_irq = platform_get_irq(pdev, 0);
	if (g_ld_dipl_driver_irq < 0) {
		pr_err("invalid IRQ\n");
		goto bad_exit_iounmap;
	}
	pr_info("interrutp is: %d\n", g_ld_dipl_driver_irq);

	/* register interrupt handler */
	ret = request_irq(g_ld_dipl_driver_irq,
			      ld_dipl_isr,
			      0,
			      ld_dipl_driver.driver.name,
			      &ld_dipl_driver);
	if (ret < 0) {
		pr_err("unable to request IRQ\n");
		goto bad_exit_iounmap;
	}

	/* create the sysfs entries */
	ret = driver_create_file(&ld_dipl_driver.driver,
				     &driver_attr_irq_mask);
	if (ret != 0) {
		pr_err("failed to create irq_mask sysfs entry");
		goto bad_exit_remove_irq_mask;
	}

	ret = driver_create_file(&ld_dipl_driver.driver,
				     &driver_attr_keys);
	if (ret != 0) {
		pr_err("failed to create keys sysfs entry");
		goto bad_exit_remove_keys;
	}

	ret = driver_create_file(&ld_dipl_driver.driver,
				     &driver_attr_leds);
	if (ret != 0) {
		pr_err("failed to create leds sysfs entry");
		goto bad_exit_remove_leds;
	}

	ret = driver_create_file(&ld_dipl_driver.driver,
				     &driver_attr_irq_flag);
	if (ret != 0) {
		pr_err("failed to create irq_flag sysfs entry");
		goto bad_exit_remove_irq_flag;
	}
	pr_info("probe exit success\n");	
	return 0;

bad_exit_remove_irq_flag:
	driver_remove_file(&ld_dipl_driver.driver,
			   &driver_attr_irq_flag);
bad_exit_remove_leds:
	driver_remove_file(&ld_dipl_driver.driver,
			   &driver_attr_leds);
bad_exit_remove_keys:
	driver_remove_file(&ld_dipl_driver.driver,
			   &driver_attr_keys);
bad_exit_remove_irq_mask:
	driver_remove_file(&ld_dipl_driver.driver,
			   &driver_attr_irq_mask);
bad_exit_freeirq:
	free_irq(g_ld_dipl_driver_irq, &ld_dipl_driver);
bad_exit_iounmap:
	iounmap(g_ld_dipl_driver_base_addr);
bad_exit_release_mem_region:
	release_mem_region(g_driver_mem_base_addr, g_driver_mem_size);

	pr_info("probe exit fail\n");
	return ret;
}

static int ld_dipl_remove(struct platform_device *pdev)
{
	pr_info("remove enter\n");
	driver_remove_file(&ld_dipl_driver.driver,
			   &driver_attr_irq_flag);
	driver_remove_file(&ld_dipl_driver.driver,
			   &driver_attr_leds);
	driver_remove_file(&ld_dipl_driver.driver,
			   &driver_attr_keys);
	driver_remove_file(&ld_dipl_driver.driver,
			   &driver_attr_irq_mask);
	driver_remove_file(&ld_dipl_driver.driver,
			   &driver_attr_irq_flag);
	driver_remove_file(&ld_dipl_driver.driver,
			   &driver_attr_leds);
	driver_remove_file(&ld_dipl_driver.driver,
			   &driver_attr_keys);
	driver_remove_file(&ld_dipl_driver.driver,
			   &driver_attr_irq_mask);
	free_irq(g_ld_dipl_driver_irq, &ld_dipl_driver);

	iounmap(g_ld_dipl_driver_base_addr);

	release_mem_region(g_driver_mem_base_addr, g_driver_mem_size);

	pr_info("remove exit\n");
	return 0;
}

static struct platform_driver ld_dipl_driver = {
	.probe = ld_dipl_probe,
	.remove = ld_dipl_remove,
	.driver = {
		.name = DRIVER_NAME,
		.of_match_table = ld_dipl_of_match,
	},
};

static int __init ld_dipl_init(void)
{
	int ret;

	pr_info("init enter\n");

	ret = platform_driver_register(&ld_dipl_driver);
	if (ret != 0)
		pr_err("platform_driver_register returned %d\n", ret);
	pr_info("init exit\n");

	return ret;
}

static void __exit ld_dipl_exit(void)
{
	pr_info("ld_dipl_exit enter\n");
	platform_driver_unregister(&ld_dipl_driver);
	pr_info("ld_dipl_exit exit\n");
}

module_init(ld_dipl_init);
module_exit(ld_dipl_exit);

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("ETF Diploma Thesis FPGA Driver and Device");
MODULE_AUTHOR("Dejan Lukic");
