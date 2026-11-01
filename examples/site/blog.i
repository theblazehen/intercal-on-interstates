PLEASE NOTE Retrieves the specified blog post from the database,
PLEASE NOTE then renders a template using it.

DO COME FROM (16000) WITH ROUTE "/blog/post/:id"
DO STASH ,3 <- ;4400 SUB #1

NOTE Use multiple MINGLE to specify multiple columns.
NOTE Multiple columns in a FROM DATABASE clause will 
NOTE automatically fill an array.
PLEASE NOTE Specify the HAVING clause to constrain
NOTE results, in this case column #0 must equal ,3
DO STASH ;49221 <- 'FROM DATABASE #1~#3$#4$#5' HAVING #0 ,3
READ OUT #3 WITH TEMPLATE "blog_post.html"
PLEASE GIVE UP