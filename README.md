# Project 4: Web Server

Author: José Eduardo Corella

# About This Project
Are you tired of using a trusted Web Server like Nginx for all your web server needs? Or your reliable but expensive EC2 instances? Dumpster Fire server is here! This project is a multithreaded web server that is able to host web pages and able to successfully support file downloads.

# Program Options (The World's your Oyster)
-No really this is a webserver you can host minecraft on here if you want to. Although not recommended cause we weren't able to hire more developers for this-
There really aren't any. Prerequisites: You need to know how to host a webpage. Not really, I'm just kidding. This web server currently only runs on your local machine with future updates coming to be able to be accessed from the normal web. To test the server, forward the remote HTTP port to your local machine. Here, I’m forwarding port 8080 on my VM to port 8080 on my local machine. Then I can simply navigate to http://localhost:8080 to test my code:

```bash
ssh wall-e -L 8080:localhost:8080

```

## Testing
To test I have included a sample html folder that contains simple html files and are able to served using this server implementation.
```
./www 'port number' ./tests/html
```

See: https://www.cs.usfca.edu/~mmalensek/cs326/assignments/project-4.html
