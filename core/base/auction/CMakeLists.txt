ttk_add_base_library(auction

  SOURCES Auction.cpp 
  HEADERS Auction.h AuctionImpl.h AuctionActor.h
	LINK triangulation persistenceDiagram kdTree)
