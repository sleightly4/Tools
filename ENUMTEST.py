import subprocess
import os

# Directories searched by grep
directories = ['/home', '/root']

# Keywords used in search
keywords = ['password', 'username', 'user', 'pass']

# Check SUIDs
with open(os.devnull, 'w') as devnull:
    suids = subprocess.check_output(['find', '/', '-perm', '-4000'], stderr=devnull)
suids = suids.decode().strip().split('\n')
print('SUIDs:')
for suid in suids:
    print(suid)

# Check GUIDs
with open(os.devnull, 'w') as devnull:
    guids = subprocess.check_output(['find', '/', '-perm', '-2000'], stderr=devnull)
guids = guids.decode().strip().split('\n')
print('\nGUIDs:')
for guid in guids:
    print(guid)

# Search files for specific keywords
print('\nFiles containing the keywords:')
for keyword in keywords:
    try:
        with open(os.devnull, 'w') as devnull:
            grep_output = subprocess.check_output(['grep', '-r', '-w', keyword] + directories, stderr=devnull)
        grep_output = grep_output.decode().strip().split('\n')
        for line in grep_output:
            print(line)
    except subprocess.CalledProcessError:
        pass
