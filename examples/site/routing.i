        NOTE At the start of the program, handle the default route.
        DO (15) NEXT
            
(2)     DO COME FROM (16) WITH ROUTE "/blog/post/:id"

        PLEASE DO ,1 <- ;4400 SUB #1
        PLEASE NOTE ,1 now contains the first argument passed 
        NOTE to the route.

        NOTE This is where you could load up some data based upon ,1.
        NOTE For the purposes of this example, assume we loaded 
        NOTE our data into ,4, ,5, and ,6.
        DO STASH ;49221 SUB #1 <- ,4
        DO STASH ;49221 SUB #2 <- ,5
        DO STASH ;49221 SUB #3 <- ,6

        NOTE Render the template. 
        NOTE Pass #3 because there are three arguments.
        PLEASE READ OUT #3 WITH TEMPLATE "blog_post.html"
        PLEASE GIVE UP

        NOTE This is the default page of the website.
(15)    DO COME FROM (16000) WITH ROUTE "/home"
        NOTE Render the template. Pass #0 if no parameters are required.
(16)    READ OUT #0 WITH TEMPLATE "home.html"
        PLEASE GIVE UP