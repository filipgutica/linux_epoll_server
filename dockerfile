FROM gcc:latest

# Set the working directory in the container
WORKDIR /usr/src/app

# Copy the current directory contents into the container at /usr/src/app
COPY . .

# Install any necessary packages (if not included in the gcc image)
RUN apt-get update && apt-get install -y \
    make \
    && rm -rf /var/lib/apt/lists/*

# Build the project
RUN make

# Run with a default port
ENTRYPOINT ["./entrypoint.sh"]
