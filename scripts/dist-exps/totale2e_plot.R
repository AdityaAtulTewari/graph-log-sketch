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

data <- read.csv(args[1], header=T, fileEncoding="UTF-8-BOM")
data <- data_summary(data, varname="Algorithm_Duration", groupnames=c("Graph_Type", "Hosts"))
data$Graph_Type <- factor(data$Graph_Type, levels=c("adj", "lccsr", "lscsr"))

time_plot <- ggplot(data=data, aes(x=factor(Hosts), fill=factor(Graph_Type), y=Algorithm_Duration
)) +
  geom_bar(mapping=aes(x=factor(Hosts), fill=factor(Graph_Type), y=Algorithm_Duration),
           position=position_dodge(), stat="identity") +
  labs(fill="Graph Type", y="End-To-End Execution time (s)", x="Number of Hosts")+
  theme(axis.title = element_text(color="black", size=20),
        axis.text.y = element_text(color="black", size=20),
        axis.text.x = element_text(color="black", size=20),
        axis.title.y = element_text(margin=margin(t=0, r=0, l=20, b=0), size=20),
        axis.title.x = element_text(margin=margin(t=20, r=0, l=0, b=0), size=20)) +
  scale_fill_manual(values = c("adj"="#FF6600", "lccsr"="66", "lscsr"="blue")) +
  geom_text(aes(label=sprintf("%1.2f", Algorithm_Duration)),
  position=position_dodge(1), vjust=0) +
  #theme_minimal() +
  geom_errorbar(aes(ymin=Algorithm_Duration
                    -sd, ymax=Algorithm_Duration
                    +sd), width=.2, position=position_dodge(.9))

ggsave(args[2], height=6, width=10)
