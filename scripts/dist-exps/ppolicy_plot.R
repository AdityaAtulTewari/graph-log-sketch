library("optparse")
library("data.table")
library(dplyr)
library(ggplot2)
library(Rmisc)
library(RColorBrewer)
library(ggh4x)

args=commandArgs(trailingOnly=TRUE)

data_summary <- function(data, varname, groupnames) {
  require(plyr)
  summary_func <- function(x, col) {
    c(mean = mean(x[[col]], na.rm=TRUE), sd = sd(x[[col]], na.rm=TRUE))
  }
  data_sum <- ddply(data, groupnames, .fun=summary_func, varname)
  data_sum <- rename(data_sum, c("mean" = varname))
  return (data_sum)
}

num_hosts <- args[1]
data <- read.csv(args[2], header=T, fileEncoding="UTF-8-BOM")
data <- data %>% filter(data$Hosts == num_hosts)
data <- data_summary(data, varname="Algorithm_Duration", groupnames=c("Policy", "Hosts", "HostID"))
data$Policy <- factor(data$Policy, levels=c("oec", "divija", "cvc"))

time_plot <- ggplot(data=data, aes(x=factor(Policy), fill=factor(HostID), y=Algorithm_Duration
)) +
  geom_bar(mapping=aes(x=factor(Policy), fill=factor(HostID), y=Algorithm_Duration),
           position=position_dodge(), stat="identity") +
  labs(fill="Number of Hosts", y="Total Computation Time (s)", x="Graph Partitioning Policies")+
  theme(axis.title = element_text(color="black", size=20),
        axis.text.y = element_text(color="black", size=20),
        axis.text.x = element_text(color="black", size=20, angle=90),
        axis.title.y = element_text(margin=margin(t=0, r=0, l=20, b=0), size=20),
        axis.title.x = element_text(margin=margin(t=20, r=0, l=0, b=0), size=20)) +
  #scale_fill_manual(values = c("oec"="#FF6600", "divija"="66", "cvc"="blue")) +
  geom_text(aes(label=sprintf("%1.2f", Algorithm_Duration)),
            position=position_dodge(1), vjust=0) +
  #theme_minimal() +
  geom_errorbar(aes(ymin=Algorithm_Duration
                    -sd, ymax=Algorithm_Duration
                    +sd), width=.2, position=position_dodge(.9))

ggsave(args[3], height=6, width=6)

