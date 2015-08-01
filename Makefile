OBJS = main.o
gemini:$(OBJS)
	$(CC) $< -o $@

clean:
	rm -rf $(OBJS)
