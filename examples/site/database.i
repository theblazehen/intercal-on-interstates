NOTE Retrieves row 1 from table 0 into ;1, then extracts 
NOTE column 3, then renders a template using it.
DO COME FROM (16000)

NOTE Use sparks to create an order of operations.
PLEASE DO STASH ;49221 SUB #1 <- 'FROM DATABASE #0~#1' SUB #3
DO READ OUT #1 WITH TEMPLATE "db_sample.html"
PLEASE GIVE UP