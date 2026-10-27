Cells are 8 bits and wrap around in both directions
-.            print byte 255
+.            wraps to 0 so print byte 0
>++++++++[<++++++++>-]<+.    print A (65)
[-]-[+]       count up from 255 to 0 with a loop
++++++++[>+++++<-]>++.       print a star (42)
