#include <linux/init.h>
#include <linux/module.h>
#include <linux/fs.h>
#include <linux/cdev.h>
#include <linux/slab.h>
#include <linux/semaphore.h>
#include <linux/spinlock.h>
#include <linux/mutex.h>
#include <linux/device.h>
#include <linux/sched.h>
#include <linux/list.h>
#include <linux/uaccess.h>
#include <linux/syscalls.h>

#define DEVICE_NAME "BARRIER"

struct barrier{
	unsigned int barrier_id;
	unsigned int count;			   			/* Total number of threads */
	unsigned int curr_count;
	spinlock_t count_lock;
	struct semaphore barrier_sem;

	unsigned int pre_entry_lock_count;
	unsigned int entry_lock_count;
	struct mutex entry_lock;		/*To stop threads {1 ... - last-1} dont run because of up from last thread in barrier_wait()  */
	struct list_head barrier_list;

	pid_t thread_group_id;

	int barrier_should_stop;
};			typedef struct barrier barrier_t;

DEFINE_SPINLOCK(global_barrier_list_lock);
EXPORT_SYMBOL_GPL(global_barrier_list_lock);

unsigned int global_barrier_id = 0;
DEFINE_SPINLOCK(global_barrier_id_lock);
LIST_HEAD(global_barrier_list);
EXPORT_SYMBOL_GPL(global_barrier_list);

static int do_barrier(barrier_t* my_barrier){
	
	spin_lock(&(my_barrier -> count_lock));
	my_barrier->pre_entry_lock_count +=1;
	spin_unlock(&(my_barrier -> count_lock));

	if(mutex_lock_interruptible(&(my_barrier -> entry_lock))  != 0){
		printk("%s: mutex interrupted\n",__FUNCTION__);
		return -EINTR;
	}

	/*
	*  Ensures safety against barrier_destroy
	*  by blocking barrier_destroy till all threads exit and 
	*  then not allowing other threads to initiate another round of barrier.
	*/
	if(my_barrier->entry_lock_count == 0 && my_barrier->barrier_should_stop){
		mutex_unlock(&(my_barrier -> entry_lock));

		spin_lock(&(my_barrier -> count_lock));
		my_barrier->pre_entry_lock_count -=1;
		spin_unlock(&(my_barrier -> count_lock));

		printk("%s: id: %d, time to stop", __FUNCTION__, my_barrier->barrier_id);
		return -EINVAL;
	}

	my_barrier->entry_lock_count +=1;

	if(my_barrier->entry_lock_count < my_barrier->count){
		mutex_unlock(&(my_barrier -> entry_lock));

		spin_lock(&(my_barrier -> count_lock));
		my_barrier->pre_entry_lock_count -=1;
		spin_unlock(&(my_barrier -> count_lock));
	}

	spin_lock(&(my_barrier -> count_lock));
	my_barrier->curr_count += 1;
	if(my_barrier->curr_count < my_barrier->count){
		spin_unlock(&(my_barrier -> count_lock));
		printk("%s: pid: %d putting id:%d to sleep\n",__FUNCTION__, current->pid, my_barrier->barrier_id);
		if(down_interruptible(&(my_barrier->barrier_sem)) != 0){
			printk("sema interrupted\n");
			return -EINTR ;
		}
		printk("%s: pid: %d barrier id:%d woke up\n",__FUNCTION__, current->pid, my_barrier->barrier_id);
		spin_lock(&(my_barrier -> count_lock));
	}
	else{
		printk("%s: pid: %d waking up all threads in barrier id %d\n",__FUNCTION__, current->pid, my_barrier->barrier_id);
	}
	/* This section has to be guarded with a spinlock for thread safety for SMP machines */
	/* because another thread on another core can potentially call up. */
	if(my_barrier->curr_count > 0){
		unsigned int curr_count = my_barrier->curr_count - 1; /* 1 less than curr_count to account for current thread */
		printk("%s: curr_count= %d\n",__FUNCTION__, my_barrier->curr_count);
		while(curr_count){
			up(&(my_barrier->barrier_sem));
			//printk("%s: woken up by %d\n",__FUNCTION__, current->pid);
			curr_count -= 1;
		}
		my_barrier->curr_count = 0;
	}

	/* The spinlock also guards entry_lock_count. */
	my_barrier->entry_lock_count -=1;

	/* The last thread unlocks the entry mutex. */
	if(my_barrier->entry_lock_count == 0){
		mutex_unlock(&(my_barrier -> entry_lock));

		spin_lock(&(my_barrier -> count_lock));
		my_barrier->pre_entry_lock_count -=1;
		spin_unlock(&(my_barrier -> count_lock));
	}
	spin_unlock(&(my_barrier -> count_lock));

	return 0;
}

barrier_t* search_from_global_list(const unsigned int id){
	barrier_t* curr_barrier= NULL;

	spin_lock(&global_barrier_list_lock);
	list_for_each_entry(curr_barrier, &global_barrier_list, barrier_list){

		/* don't search for barriers that would be destroyed */
		if((curr_barrier->curr_count == 0) && curr_barrier->barrier_should_stop == 1 )
			continue;

		if(curr_barrier->barrier_id == id && curr_barrier->thread_group_id == current->tgid){
			spin_unlock(&global_barrier_list_lock);
			return curr_barrier;
		}
	}
	spin_unlock(&global_barrier_list_lock);

	return (void*)-EINVAL;
}

/*barrier_wait*/
asmlinkage long sys_barrier_wait(unsigned int barrier_id){

	barrier_t* my_barrier;

	my_barrier = search_from_global_list(barrier_id);
	if(my_barrier == (void*)-EINVAL){
		//printk("Barrier with id %d not found\n", barrier_id);
		return -EINVAL;	
	}

	return do_barrier(my_barrier);
}

long initialize_barrier(barrier_t* my_barrier, const unsigned int count){	
	my_barrier->barrier_should_stop = 0;

	my_barrier->count = count;
	my_barrier->curr_count = 0;
	sema_init(&(my_barrier -> barrier_sem), 0);
	spin_lock_init(&(my_barrier -> count_lock));

	mutex_init(&(my_barrier -> entry_lock));
	my_barrier->pre_entry_lock_count = 0;
	my_barrier->entry_lock_count = 0;

	spin_lock(&(global_barrier_id_lock));
	my_barrier->barrier_id = global_barrier_id++;
	spin_unlock(&(global_barrier_id_lock));
	
	my_barrier->thread_group_id = current->tgid;
	INIT_LIST_HEAD(&(my_barrier->barrier_list));

	spin_lock(&global_barrier_list_lock);
	list_add_tail(&(my_barrier->barrier_list), &global_barrier_list);
	spin_unlock(&global_barrier_list_lock);

	printk("\n********************************************************************\n");
	printk("%s: barrier id= %u, count= %d initialized\n", __FUNCTION__, my_barrier->barrier_id, my_barrier->count);
	return 0;
}

/*barrier_init*/
asmlinkage long sys_barrier_init(unsigned int count, unsigned int *barrier_id){

	unsigned int thread_count;
	long ret;
	barrier_t* my_barrier = kzalloc(sizeof(barrier_t), GFP_KERNEL);
	if(my_barrier == NULL)
		return -ENOMEM;

	thread_count = count;
	ret = initialize_barrier(my_barrier, thread_count);
	if(ret < 0)
		return ret;

	if(copy_to_user(barrier_id, &(my_barrier->barrier_id), sizeof(unsigned int)) != 0)
		return -EFAULT;

	return 0;
}


/*TO DO: Remove this */
asmlinkage long sys_barrier_destroy(unsigned int barrier_id){
	barrier_t* my_barrier;

	my_barrier = search_from_global_list(barrier_id);
	if(my_barrier == (void*)-EINVAL){ 
		printk("Barrier with id %d not found\n", barrier_id);
		return -EINVAL;	
	}
	for(;;){ 
		/* Ensures no other thread is using the barrier */
		if(mutex_lock_interruptible(&(my_barrier->entry_lock)) != 0){
			printk("%s: mutex interrupted\n",__FUNCTION__);
			return -EINTR;
		}

		my_barrier->barrier_should_stop = 1;

		if(my_barrier->curr_count == 0)
			break;

		/*Corner case: when other threads are inside the lock and a participating thread calls destroy instead of wait !*/
		if(my_barrier->curr_count == my_barrier->count - 1){
			unsigned int curr_count = my_barrier->curr_count;
			while(curr_count){
				up(&(my_barrier->barrier_sem));
				curr_count -= 1;
			}
			my_barrier->curr_count = 0;
			schedule();
		}

		mutex_unlock(&(my_barrier->entry_lock));
		schedule();
	}

	spin_lock(&global_barrier_list_lock);
	list_del_init(&(my_barrier->barrier_list));
	spin_unlock(&global_barrier_list_lock);

	mutex_unlock(&(my_barrier->entry_lock));

	while(my_barrier->pre_entry_lock_count > 0)
		schedule();

	kfree(my_barrier);

	return 0;
}

EXPORT_SYMBOL_GPL(sys_barrier_destroy);
EXPORT_SYMBOL_GPL(sys_barrier_init);
EXPORT_SYMBOL_GPL(sys_barrier_wait);

MODULE_LICENSE("GPL v2");
