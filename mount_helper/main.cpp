#include <string>
#include <iostream>
#include <vector>
#include "../stringtools.h"
#include "../urbackupcommon/os_functions.h"
#include <stdlib.h>
#include <unistd.h>
#include <stdarg.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <pwd.h>
#include <linux/loop.h>
#include <errno.h>
#include <sys/ioctl.h>
extern char **environ;

#define DEF_Server
#include "../Server.h"
#include "../config.h"

CServer *Server;


std::string getBackupfolderPath()
{
	std::string fn=trim(getFile("/etc/urbackup/backupfolder"));
	if(fn.find("\n")!=std::string::npos)
		fn=getuntil("\n", fn);
	if(fn.find("\r")!=std::string::npos)
		fn=getuntil("\r", fn);
	
	return fn;
}

std::string handleFilename(std::string fn)
{
	fn=conv_filename(fn);
	if(fn=="..")
	{
		return "";
	}
	return fn;
}

int exec_wait(const std::string& path, bool keep_stdout, ...)
{
	va_list vl;
	va_start(vl, keep_stdout);
	
	std::vector<char*> args;
	args.push_back(const_cast<char*>(path.c_str()));
	
	while(true)
	{
		const char* p = va_arg(vl, const char*);
		if(p==NULL) break;
		args.push_back(const_cast<char*>(p));
	}
	va_end(vl);
	
	args.push_back(NULL);
	
	pid_t child_pid = fork();
	
	if(child_pid==0)
	{
		environ = new char*[1];
		*environ=NULL;
		
		if(!keep_stdout)
		{
			int nullfd = open("/dev/null", O_WRONLY);
			
			if(nullfd!=-1)
			{
				if(dup2(nullfd, 1)==-1)
				{
					return -1;
				}
				
				if(dup2(nullfd, 2)==-1)
				{
					return -1;
				}
			}
			else
			{
				return -1;
			}
		}
		
		int rc = execvp(path.c_str(), args.data());
		exit(rc);
	}
	else
	{
		int status;
		waitpid(child_pid, &status, 0);
		if(WIFEXITED(status))
		{
			return WEXITSTATUS(status);
		}
		else
		{
			return -1;
		}
	}
}

std::string find_urbackupsrv_cmd()
{
	static std::string urbackupsrv_cmd;
	
	if(!urbackupsrv_cmd.empty())
	{
		return urbackupsrv_cmd;
	}
	
	if(exec_wait("urbackupsrv", false, "--version", NULL)==1)
	{
		urbackupsrv_cmd="urbackupsrv";
		return urbackupsrv_cmd;
	}
	else if(exec_wait("/sbin/urbackupsrv", false, "--version", NULL)==1)
	{
		urbackupsrv_cmd="/sbin/urbackupsrv";
		return urbackupsrv_cmd;
	}
	else if(exec_wait("/bin/urbackupsrv", false, "--version", NULL)==1)
	{
		urbackupsrv_cmd="/bin/urbackupsrv";
		return urbackupsrv_cmd;
	}
	else if(exec_wait("/usr/sbin/urbackupsrv", false, "--version", NULL)==1)
	{
		urbackupsrv_cmd="/usr/sbin/urbackupsrv";
		return urbackupsrv_cmd;
	}
	else if(exec_wait("/usr/bin/urbackupsrv", false, "--version", NULL)==1)
	{
		urbackupsrv_cmd="/usr/bin/urbackupsrv";
		return urbackupsrv_cmd;
	}
	else
	{
		urbackupsrv_cmd="urbackupsrv";
		return urbackupsrv_cmd;
	}
}



bool chown_dir(const std::string& dir)
{
	passwd* user_info = getpwnam("urbackup");
	if(user_info)
	{
		int rc = chown(dir.c_str(), user_info->pw_uid, user_info->pw_gid);
		return rc!=-1;
	}
	return false;
}

bool mount_image(const std::string& imagepath)
{
	std::string ext = findextension(imagepath);
	
	std::string mount_options="";
	passwd* user_info = getpwnam("urbackup");
	if(user_info)
	{
		mount_options+="uid="+convert(user_info->pw_uid)+",gid="+convert(user_info->pw_gid)+",allow_root";
	}
	
	if(ext=="raw")
	{
		int loopc = open("/dev/loop-control", O_RDWR|O_CLOEXEC);
		if(loopc==-1)
		{
			exec_wait("modprobe", true, "loop", NULL);
			loopc = open("/dev/loop-control", O_RDWR|O_CLOEXEC);
		}
		
		if(loopc==-1)
		{
			std::cerr << "Error opening loop control. Err: " << errno << std::endl;
			return false;
		}
		
		int bd = open(imagepath.c_str(), O_RDONLY|O_CLOEXEC);
		if(bd==-1)
		{
			std::cerr << "Error opening backing file " << imagepath << std::endl;
			close(loopc);
			return false;
		}
		
		int last_loopd=-1;
		int devnum;
		int last_devnum = -1;
		int loopd = -1;
		bool set_success=false;
		do
		{
			devnum = ioctl(loopc, LOOP_CTL_GET_FREE);
			if(devnum<0)
			{
				std::cerr << "Error getting free loop device. Err: " << errno << std::endl;
				break;
			}
			
			if(devnum==last_devnum)
			{
				std::cerr << "Getting same loop device after EBUSY. Stopping." << std::endl;
				break;
			}
			
			last_devnum = devnum;
			
			loopd = open(("/dev/loop"+convert(devnum)).c_str(), O_RDWR|O_CLOEXEC);
			if(loopd==-1)
			{
				std::cerr << "Error opening loop device " << imagepath << std::endl;
				break;
			}
			
			int rc = ioctl(loopd, LOOP_SET_FD, bd);
			if(rc)
			{
				if(errno!=EBUSY)
				{
					std::cerr << "Error setting loop device fd. Err: " << errno << std::endl;
					break;
				}
				else
				{
					close(loopd);
					loopd=-1;
				}
			}
			else
			{
				set_success=true;
			}			
		} while(!set_success);
		
		close(loopc);
		close(bd);
		
		if(!set_success)
		{		
			if(loopd!=-1)
			{
				close(loopd);
			}
			return false;
		}
		
		loop_info64 linfo = {};
		linfo.lo_offset = 512*1024;
		linfo.lo_flags = LO_FLAGS_READ_ONLY|LO_FLAGS_AUTOCLEAR|LO_FLAGS_DIRECT_IO;
		int rc = ioctl(loopd, LOOP_SET_STATUS64, &linfo);
		if(rc)
		{
			std::cerr << "Error setting loop device status. Err: " << errno << std::endl;
			close(loopd);
			return false;
		}
		
		std::string mountpoint = ExtractFilePath(imagepath)+"_mnt";
		
		if(!os_directory_exists(mountpoint)
			&& !os_create_dir(mountpoint))
		{
			std::cerr << "Error creating mountpoint at \"" << mountpoint << "\". Err: " << errno << std::endl;
			close(loopd);
			return false;
		}

		chown_dir(mountpoint);
		chown_dir("/dev/loop"+convert(devnum));

		std::cout << "Guestmount..." << std::endl;
		if(exec_wait("guestmount", true, "-r", "--format=raw", "-a", ("/dev/loop"+convert(devnum)).c_str(),
					"-o", mount_options.c_str(),
					"-m", "/dev/sda",
					mountpoint.c_str(), NULL) )
		{
			close(loopd);
			os_remove_dir(mountpoint);
			return false;
		}	

		close(loopd);
		return true;
	}
	else
	{
		std::string mountpoint = ExtractFilePath(imagepath)+"/contents";

		if(os_directory_exists(mountpoint) || errno==EACCES ) 
		{
			exec_wait("guestunmount", true, mountpoint.c_str(), NULL);
		}
	
		if(!os_directory_exists(mountpoint)
			&& !os_create_dir(mountpoint))
		{
			std::cerr << "Error creating mountpoint at \"" << mountpoint << "\". Err: " << errno << std::endl;
			return false;
		}

		chown_dir(mountpoint);
		
		std::string devpoint = ExtractFilePath(imagepath)+"/device";

		if(os_directory_exists(devpoint) || errno==EACCES )
		{
			if(exec_wait("fusermount", true, "-u", devpoint.c_str(), NULL))
	                {
        	                exec_wait("fusermount", true, "-u", "-z", devpoint.c_str(), NULL);
                	}
		}
		
		if(!os_directory_exists(devpoint)
			&& !os_create_dir(devpoint))
		{
			std::cerr << "Error creating devpoint at \"" << devpoint << "\". Err: " << errno << std::endl;
			os_remove_dir(mountpoint);
			return false;
		}

		chown_dir(devpoint);
		
		if(exec_wait(find_urbackupsrv_cmd(), true, "mount-vhd", "-f", imagepath.c_str(), "-m", mountpoint.c_str(), "-t", devpoint.c_str(), "-o", mount_options.c_str(), "--guestmount", NULL))
		{
			std::cout << "UrBackup mount process returned non-zero return code" << std::endl;
			os_remove_dir(mountpoint);
			os_remove_dir(devpoint);
			return false;
		}
		
		return true;
	}
}

bool unmount_image(const std::string& imagepath)
{
	std::string ext = findextension(imagepath);
	std::string mountpoint;
	if(ext=="raw")
	{
		mountpoint = ExtractFilePath(imagepath)+"_mnt";
	}
	else
	{
		mountpoint = ExtractFilePath(imagepath)+"/contents";
	}

	std::cout << "Mountpoint: " << mountpoint << std::endl;

	bool ret=true;

	if(os_directory_exists(mountpoint) || errno==EACCES )
	{
		std::cout << "Guestunmount..." << std::endl;
		if(exec_wait("guestunmount", true, mountpoint.c_str(), NULL))
		{
			std::cerr << "Unmounting \"" << mountpoint << "\" failed." << std::endl;
			ret = false;
		}
		
		os_remove_dir(mountpoint);
	}
	
	std::string devpoint = ExtractFilePath(imagepath)+"/device";
	
	if(ext!="raw" && (os_directory_exists(devpoint) || errno==EACCES ) )
	{
		if(exec_wait("fusermount", true, "-u", devpoint.c_str(), NULL))
		{
			exec_wait("fusermount", true, "-u", "-z", devpoint.c_str(), NULL);
		}
		
		os_remove_dir(devpoint);
	}
	
	return ret;
}

int main(int argc, char *argv[])
{
	if(argc<2)
	{
		std::cout << "Not enough parameters" << std::endl;
		return 1;
	}

	std::string backupfolder=getBackupfolderPath();
	
	if(backupfolder.empty())
	{	
		std::cout << "Backupfolder not set" << std::endl;
		return 1;
	}
	
	if(seteuid(0)!=0)
	{
		std::cout << "Cannot become root user" << std::endl;
		return 1;
	}

	std::string cmd = argv[1];
	if(cmd=="mount")
	{
		if(argc<4)
		{
			std::cout << "Not enough parameters for mount" << std::endl;
			return 1;
		}

		std::string clientname=handleFilename(argv[2]);
		std::string name=handleFilename(argv[3]);
		std::string imagename=handleFilename(argv[4]);

		return mount_image(backupfolder + os_file_sep() + clientname + os_file_sep() + name + os_file_sep() + imagename)?0:1;
	}
	else if(cmd=="umount")
	{
		if(argc<5)
		{
			std::cout << "Not enough parameters for umount" << std::endl;
			return 1;
		}

		std::string clientname=handleFilename(argv[2]);
		std::string name=handleFilename(argv[3]);
		std::string imagename=handleFilename(argv[4]);

		return unmount_image(backupfolder + os_file_sep() + clientname + os_file_sep() + name + os_file_sep() + imagename)?0:1;
	}
	else if(cmd=="test")
	{
#ifndef WITH_FUSEPLUGIN
		std::cerr << "TEST FAILED: Please compile with mountvhd (./configure --with-mountvhd)" << std::endl;
		return 1;
#endif
		
		if(system("guestmount --version > /dev/null 2>&1")!=0)
		{
			std::cerr << "TEST FAILED: guestmount is missing (libguestfs-tools)" << std::endl;
			return 1;
		}
		return 0;
	}
	else
	{
		std::cout << "Command not found" << std::endl;
		return 1;
	}
}

