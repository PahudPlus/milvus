package memms

import (
	"context"
	"log"
	"testing"

	"github.com/stretchr/testify/assert"
	"github.com/zilliztech/milvus-distributed/internal/msgstream"
	"github.com/zilliztech/milvus-distributed/internal/proto/commonpb"
	"github.com/zilliztech/milvus-distributed/internal/proto/internalpb"
)

func getTsMsg(msgType MsgType, reqID UniqueID, hashValue uint32) TsMsg {
	baseMsg := BaseMsg{
		BeginTimestamp: 0,
		EndTimestamp:   0,
		HashValues:     []uint32{hashValue},
	}
	switch msgType {
	case commonpb.MsgType_Search:
		searchRequest := internalpb.SearchRequest{
			Base: &commonpb.MsgBase{
				MsgType:   commonpb.MsgType_Search,
				MsgID:     reqID,
				Timestamp: 11,
				SourceID:  reqID,
			},
			Query:           nil,
			ResultChannelID: "0",
		}
		searchMsg := &msgstream.SearchMsg{
			BaseMsg:       baseMsg,
			SearchRequest: searchRequest,
		}
		return searchMsg
	case commonpb.MsgType_SearchResult:
		searchResult := internalpb.SearchResults{
			Base: &commonpb.MsgBase{
				MsgType:   commonpb.MsgType_SearchResult,
				MsgID:     reqID,
				Timestamp: 1,
				SourceID:  reqID,
			},
			Status:          &commonpb.Status{ErrorCode: commonpb.ErrorCode_Success},
			ResultChannelID: "0",
		}
		searchResultMsg := &msgstream.SearchResultMsg{
			BaseMsg:       baseMsg,
			SearchResults: searchResult,
		}
		return searchResultMsg
	}
	return nil
}

func createProducer(channels []string) *MemMsgStream {
	InitMmq()
	produceStream, err := NewMemMsgStream(context.Background(), 1024)
	if err != nil {
		log.Fatalf("new msgstream error = %v", err)
	}
	produceStream.AsProducer(channels)
	produceStream.Start()

	return produceStream
}

func createCondumers(channels []string) []*MemMsgStream {
	consumerStreams := make([]*MemMsgStream, 0)
	for _, channel := range channels {
		consumeStream, err := NewMemMsgStream(context.Background(), 1024)
		if err != nil {
			log.Fatalf("new msgstream error = %v", err)
		}

		thisChannel := []string{channel}
		consumeStream.AsConsumer(thisChannel, channel+"_consumer")
		consumerStreams = append(consumerStreams, consumeStream)
	}

	return consumerStreams
}

func TestStream_GlobalMmq_Func(t *testing.T) {
	channels := []string{"red", "blue", "black", "green"}
	produceStream := createProducer(channels)
	defer produceStream.Close()

	consumerStreams := createCondumers(channels)

	// validate channel and consumer count
	assert.Equal(t, len(Mmq.consumers), len(channels), "global mmq channel error")
	for _, consumers := range Mmq.consumers {
		assert.Equal(t, len(consumers), 1, "global mmq consumer error")
	}

	// validate msg produce/consume
	msg := msgstream.MsgPack{}
	err := Mmq.Produce(channels[0], &msg)
	if err != nil {
		log.Fatalf("global mmq produce error = %v", err)
	}
	cm, _ := consumerStreams[0].Consume()
	assert.Equal(t, cm, &msg, "global mmq consume error")

	err = Mmq.Broadcast(&msg)
	if err != nil {
		log.Fatalf("global mmq broadcast error = %v", err)
	}
	for _, cs := range consumerStreams {
		cm, _ := cs.Consume()
		assert.Equal(t, cm, &msg, "global mmq consume error")
	}

	// validate consumer close
	for _, cs := range consumerStreams {
		cs.Close()
	}
	assert.Equal(t, len(Mmq.consumers), len(channels), "global mmq channel error")
	for _, consumers := range Mmq.consumers {
		assert.Equal(t, len(consumers), 0, "global mmq consumer error")
	}

	// validate channel destroy
	for _, channel := range channels {
		Mmq.DestroyChannel(channel)
	}
	assert.Equal(t, len(Mmq.consumers), 0, "global mmq channel error")
}

func TestStream_MemMsgStream_Produce(t *testing.T) {
	channels := []string{"red", "blue", "black", "green"}
	produceStream := createProducer(channels)
	defer produceStream.Close()

	consumerStreams := createCondumers(channels)
	for _, cs := range consumerStreams {
		defer cs.Close()
	}

	msgPack := msgstream.MsgPack{}
	var hashValue uint32 = 2
	msgPack.Msgs = append(msgPack.Msgs, getTsMsg(commonpb.MsgType_Search, 1, hashValue))
	err := produceStream.Produce(context.Background(), &msgPack)
	if err != nil {
		log.Fatalf("new msgstream error = %v", err)
	}

	msg, _ := consumerStreams[hashValue].Consume()
	if msg == nil {
		log.Fatalf("msgstream consume error")
	}

	produceStream.Close()
}

func TestStream_MemMsgStream_BroadCast(t *testing.T) {
	channels := []string{"red", "blue", "black", "green"}
	produceStream := createProducer(channels)
	defer produceStream.Close()

	consumerStreams := createCondumers(channels)
	for _, cs := range consumerStreams {
		defer cs.Close()
	}

	msgPack := msgstream.MsgPack{}
	msgPack.Msgs = append(msgPack.Msgs, getTsMsg(commonpb.MsgType_Search, 1, 100))
	err := produceStream.Broadcast(context.Background(), &msgPack)
	if err != nil {
		log.Fatalf("new msgstream error = %v", err)
	}

	for _, consumer := range consumerStreams {
		msg, _ := consumer.Consume()
		if msg == nil {
			log.Fatalf("msgstream consume error")
		}
	}
}