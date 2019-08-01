#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/proc_fs.h>
#include <linux/seq_file.h>
#include <linux/errno.h>
#include <linux/sched.h>
#include <linux/list.h>
#include <asm/pgtable.h>
#include <linux/slab.h>
#include <linux/seq_file.h>

#define INT_DECIMAL_STRING_SIZE(int_type) ((__CHAR_BIT__*sizeof(int_type)-1)*10/33+3)

// function declarations
unsigned long virt2phys(struct mm_struct *mm, unsigned long vpage);
char *int_to_string_alloc(int x);

// buffers used to write to proc file
char * buf;
char * tmp;

// helper function for proc file creation
static int proc_report_show(struct seq_file *m, void *v) {
	seq_printf(m, "%s\n", buf);
	return 0;
}

// yet another helper function for proc file creation
static int proc_report_open(struct inode *inode, struct  file *file) {
	return single_open(file, proc_report_show, NULL);
}

// file operations for the proc file
static const struct file_operations proc_report_fops = {
	.owner = THIS_MODULE,
	.open = proc_report_open,
	.read = seq_read,
	.llseek = seq_lseek,
	.release = single_release,
};

// intialization function for the module, also does all the work
static int __init proc_report_init(void) {
	struct task_struct *task;
	struct vm_area_struct *vma;
	unsigned long phys = 0, vpage = 0, last_vpage_phys = 0;
	int contig = 0, noncontig = 0, total_contig = 0, total_noncontig = 0;
	buf = kmalloc(sizeof(char) * 100000, GFP_KERNEL); // should be large enough buffer (add more if out of memory)
	tmp = kmalloc(sizeof(char) * 1000, GFP_KERNEL);

	// prints start of proc file
	strcpy(buf, "");
	tmp = "PROCESS REPORT:\n";
	strcat(buf,tmp);
	printk(KERN_INFO "%s", tmp);
	tmp = "proc_id,proc_name,contig_pages,noncontig_pages,total_pages\n";
	strcat(buf,tmp);
	printk(KERN_INFO "%s", tmp);

	// iterates through process list grabbing task struct
	task = current;
	task = list_first_entry(task->tasks.next, struct task_struct, tasks);
	do {
		task = list_entry(task->tasks.next, struct task_struct, tasks);
		if (task->pid >= 650) {
			vma = 0;
			// grabs virtual mapping of the process
			if (task->mm && task->mm->mmap)
				for (vma = task->mm->mmap; vma; vma = vma->vm_next)
					for (vpage = vma->vm_start; vpage < vma->vm_end; vpage += PAGE_SIZE) {
						// converts virtual address to physical address
						phys = virt2phys(task->mm, vpage);
						if (phys != 0) { // if mapped
							if (last_vpage_phys == phys + PAGE_SIZE) //if contiguous
								contig++;
							else noncontig++;
							last_vpage_phys = virt2phys(task->mm, vpage); // stored for comparison in next iteration
						}
					}
			// prints related process info and page counts to syslog
			printk(KERN_INFO "%d,%s,%d,%d,%d", task->pid, task->comm, contig, noncontig, contig+noncontig);

			// does the same as above but prints to buffer
			tmp = int_to_string_alloc(task->pid);
			strcat(buf,tmp);
			tmp = ",";
			strcat(buf,tmp);
			tmp = task->comm;
			strcat(buf,tmp);
			tmp = ",";
			strcat(buf,tmp);
			tmp = int_to_string_alloc(contig);
			strcat(buf,tmp);
			tmp = ",";
			strcat(buf,tmp);
			tmp = int_to_string_alloc(noncontig);
			strcat(buf,tmp);
			tmp = ",";
			strcat(buf,tmp);
			tmp = int_to_string_alloc(contig+noncontig);
			strcat(buf,tmp);
			tmp = "\n";
			strcat(buf,tmp);

			// add current process totals to gobal totals
			total_contig += contig; total_noncontig += noncontig;
			// reset counts for next interation
			contig = 0; noncontig = 0;
		}
	} while (task->pid != 0); // stops when process LL circles back to head

	// prints totals to syslog
	printk(KERN_INFO "TOTALS,,%d,%d,%d", total_contig, total_noncontig, total_contig+total_noncontig);
	printk(KERN_INFO "");

	// same as above but prints to buffer
	tmp = "TOTALS,,";
	strcat(buf,tmp);
	tmp = int_to_string_alloc(total_contig);
	strcat(buf,tmp);
	tmp = ",";
	strcat(buf,tmp);
	tmp = int_to_string_alloc(total_noncontig);
	strcat(buf,tmp);
	tmp = ",";
	strcat(buf,tmp);
	tmp = int_to_string_alloc(total_contig+total_noncontig);
	strcat(buf,tmp);
	tmp = "\n";
	strcat(buf,tmp);

	// creates proc file
	proc_create("proc_report", 0, NULL, &proc_report_fops);
	return 0;
}

// helper function to convert virtual address to physical address on a 5 level page table
unsigned long virt2phys(struct mm_struct *mm, unsigned long vpage){
	unsigned long int phys;
	pgd_t *pgd; p4d_t *p4d; pud_t *pud; pmd_t *pmd; pte_t *pte;
	struct page *page;

	pgd = pgd_offset(mm, vpage);
	if (pgd_none(*pgd) || pgd_bad(*pgd))
		return 0;
	p4d = p4d_offset(pgd, vpage);
	if (p4d_none(*p4d) || p4d_bad(*p4d))
		return 0;
	pud = pud_offset(p4d, vpage);
	if (pud_none(*pud) || pud_bad(*pud))
		return 0;
	pmd = pmd_offset(pud, vpage);
	if (pmd_none(*pmd) || pmd_bad(*pmd))
		return 0;
	if (!(pte = pte_offset_map(pmd, vpage)))
		return 0;
	if (!(page = pte_page(*pte)))
		return 0;
	phys = page_to_phys(page);
	pte_unmap(pte);
	return phys;
}

// exit function for the module
static void __exit proc_report_exit(void) {
	printk(KERN_INFO "");
	remove_proc_entry("proc_report", NULL);
}

// helper function to convert int to string as we have no stdlib
char *int_to_string_alloc(int x) {
	size_t len;
	int i = x;
	char *s;
	char buf[INT_DECIMAL_STRING_SIZE(int)];
	char *p = &buf[sizeof buf - 1];
	*p = '\0';
	if (i >= 0) {
		i = -i;
	}
	do {
		p--;
		*p = (char) ('0' - i % 10);
		i /= 10;
	} while (i);
	if (x < 0) {
		p--;
		*p = '-';
	}
	len = (size_t) (&buf[sizeof buf] - p);
	s = kmalloc(len, GFP_KERNEL);
	if (s) {
		memcpy(s, p, len);
	}
	return s;
}

// "main"
MODULE_LICENSE("GPL");
module_init(proc_report_init);
module_exit(proc_report_exit);
