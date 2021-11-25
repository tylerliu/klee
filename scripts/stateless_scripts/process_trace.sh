#!/bin/bash

# Script to extract the portions of the executable trace that are relevant to the packet
# $1: This is the input trace, typically obtained by booting up an executable, running it for a specific packet 
#     and then killing it. 
# $2: This is the output trace, which is the subset of the input trace relevant to packet processing by the NF. 
#     The script looks for boundary functions depending on the type of analysis ($5). 
#     If only the NF is being analysed (verify-dpdk), the relevant portions are within the nf_core_process function
#     If we are analysing both the NF and DPDK, the relevant portion starts at the receive call in the driver and ends at the send/drop call at the driver
# $3: Input metadata file from which relevant metadata must be extracted
# $4: Output metadata file. The metadata is per instruction, so the process of extraction is same across the pairs $1,$2 and $3,$4
# $5: The type of analysis. This must be either verify-dpdk or verify-hardware. 


inp_trace=$1
op_trace=$2
inp_metadata=$3
op_metadata=$4
arg=$5




if [ "$inp_trace" -nt "$op_trace" ]; then
  echo "$inp_trace -> $op_trace"
  echo "$inp_metadata -> $op_metadata"
  if [ "$arg" == "verify-dpdk" ]; then
   if grep -q "nf_core_process" $inp_trace; then  
    START=$(grep -n -m 1 "nf_core_process" $inp_trace |sed  's/\([0-9]*\).*/\1/')
    END=$(grep -n "nf_core_process" $inp_trace | tail -1 |sed  's/\([0-9]*\).*/\1/')

   else
    echo "no packet received"
    exit
   fi

  elif [ "$arg" == "verify-libc" ]; then
    START=$(grep -n -m 1 "function_under_test" $inp_trace |sed  's/\([0-9]*\).*/\1/')
    END=$(grep -n "function_under_test" $inp_trace | tail -1 |sed  's/\([0-9]*\).*/\1/')
   
  elif [ "$arg" == "verify-hardware" ]; then
   if grep -q "rte_eth_rx_burst" $inp_trace; then  
    START=$(grep -n -m 1 "rte_eth_rx_burst" $inp_trace |sed  's/\([0-9]*\).*/\1/')
   elif grep -q "ixgbe_recv" $inp_trace; then
    START=$(grep -n -m 1 "ixgbe_recv" $inp_trace |sed  's/\([0-9]*\).*/\1/')
   else
    echo "no packet received"
    exit
   fi


   if grep -q "rte_eth_tx_burst"  $inp_trace; then 
    END=$(grep -n "rte_eth_tx_burst" $inp_trace | tail -1 |sed  's/\([0-9]*\).*/\1/')
   elif grep -q "ixgbe_xmit_pkts" $inp_trace; then
    END=$(grep -n "ixgbe_xmit_pkts" $inp_trace | tail -1 |sed  's/\([0-9]*\).*/\1/')
   else
    END=$(grep -n "exit@plt" $inp_trace | tail -1 |sed  's/\([0-9]*\).*/\1/')
   fi
  
  else # Pass your own function of interest
    START=$(grep -n -m 1 $arg $inp_trace |sed  's/\([0-9]*\).*/\1/')
    END=$(grep -n $arg $inp_trace | tail -1 |sed  's/\([0-9]*\).*/\1/')
  fi 

  QUIT=$((END+1))
  sed -n ""$START","$END"p;"$QUIT"q" $inp_trace > $op_trace
  META_START=$(((START-2)*17+2))
  META_END=$(((END-1)*17+1))
  META_QUIT=$((META_END+1))
  sed -n ""$META_START","$META_END"p;"$META_QUIT"q" $inp_metadata > $op_metadata

fi
