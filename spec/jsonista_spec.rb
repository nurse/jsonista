require "spec_helper"

RSpec.describe Jsonista do
  it "has a version number" do
    expect(Jsonista::VERSION).not_to be nil
  end

  describe ".new" do
    it "returns an instance of Jsonista::Parser" do
      expect(Jsonista::Parser.new).to be_an_instance_of(Jsonista::Parser)
    end
  end

  describe "#parse_chunk" do
    let(:parser){ Jsonista::Parser.new }
    it "returns nil" do
      expect(parser.parse_chunk("[]")).to be_nil
      parser.reset
      expect(parser.parse_chunk("{}")).to be_nil
      parser.reset
      expect(parser.parse_chunk("1")).to be_nil
      parser.reset
      expect(parser.parse_chunk("true")).to be_nil
      parser.reset
      expect(parser.parse_chunk("false")).to be_nil
      parser.reset
      expect(parser.parse_chunk("null")).to be_nil
      parser.reset
      expect(parser.parse_chunk("\"foo\"")).to be_nil
      parser.reset
      expect(parser.parse_chunk('"foo\b\/\"\\\\z"')).to be_nil
      parser.reset
      expect(parser.parse_chunk('"\u3042\u3044\u3046"')).to be_nil
      parser.reset
      expect(parser.parse_chunk('"\uD842\uDFB7\u91CE\u5BB6"')).to be_nil
    end
    it "raises error" do
      expect{ parser.parse_chunk("}") }.to raise_error(Jsonista::ParseError)
      parser.reset
      expect{ parser.parse_chunk("]") }.to raise_error(Jsonista::ParseError)
      parser.reset
      expect{ parser.parse_chunk("[1a") }.to raise_error(Jsonista::ParseError)
      parser.reset
      expect{ parser.parse_chunk("truthy") }.to raise_error(Jsonista::ParseError)
    end
    it "continues parsing" do
      expect(parser.parse_chunk("[")).to be_nil
      expect(parser.parse_chunk("]")).to be_nil
      expect(parser.parse_chunk("{")).to be_nil
      expect(parser.parse_chunk("}")).to be_nil
      expect(parser.parse_chunk('"foo')).to be_nil
      expect(parser.parse_chunk('bar"')).to be_nil
      expect(parser.parse_chunk('[123')).to be_nil
      expect(parser.parse_chunk('456]')).to be_nil
    end
  end
end
